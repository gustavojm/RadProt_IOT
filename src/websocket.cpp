#include "websocket.h"

const char *header = "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: ";

char *websocket_server::get_key(char *buf, size_t *len) {
    char *p = strstr(buf, "Sec-WebSocket-Key: ");
    if (p) {
        p = p + strlen("Sec-WebSocket-Key: ");
        char *end = strchr(p, '\r');
        if (!end) {
            return NULL;
        }
        *len = end - p;
    }
    return p;
}

char *websocket_server::create_key_accept(char *inbuf) {
    static char concat_key[128] = { 0 };
    static char hash[22] = { 0 };
    static char hash_base64[64] = { 0 };
    size_t len = 0;

    char *key = get_key(inbuf, &len);
    if (!key)
        return NULL;

    memset(concat_key, 0, sizeof(concat_key));
    size_t copy_len = (len < sizeof(concat_key) - strlen(WS_GUID) - 1) ? len : (sizeof(concat_key) - strlen(WS_GUID) - 1);
    memcpy(concat_key, key, copy_len);
    concat_key[copy_len] = '\0';
    memcpy(concat_key + copy_len, WS_GUID, strlen(WS_GUID));

    sha1_ctx_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, (uint8_t *)concat_key, copy_len + strlen(WS_GUID));
    sha1_final(&ctx, (uint8_t *)hash);

    base64_encode((uint8_t *)hash, 20, hash_base64);
    return hash_base64;
}

uint32_t websocket_server::get_message_len(uint8_t *msg) {
    uint32_t len = 0;

    if ((msg[1] & 0x7F) == 126) {
        len = (msg[2] << 8) | msg[3];
    } else if ((msg[1] & 0x7F) == 127) {
        len = 0;
    } else {
        len = msg[1] & 0x7F;
    }

    return len;
}

bool websocket_server::is_masked_msg(uint8_t *msg) {
    return (msg[1] & WS_MASKED_FLAG);
}

bool websocket_server::is_fin_msg(uint8_t *msg) {
    return (msg[0] & WS_FIN_FLAG);
}

uint8_t *websocket_server::get_mask(uint8_t *msg) {
    if ((msg[1] & 0x7F) == 126) {
        return &msg[4];
    } else if ((msg[1] & 0x7F) == 127) {
        return &msg[10];
    } else {
        return &msg[2];
    }
}

uint8_t *websocket_server::get_payload_ptr(uint8_t *msg) {
    uint8_t *p;

    if ((msg[1] & 0x7F) == 126) {
        p = msg + 4;
    } else if ((msg[1] & 0x7F) == 127) {
        p = msg + 10;
    } else {
        p = msg + 2;
    }

    if (is_masked_msg(msg)) {
        p += 4;
    }

    return p;
}

void websocket_server::unmask_message_payload(uint8_t *pld, uint32_t len, uint8_t *mask) {
    for (int i = 0; i < len; i++) {
        pld[i] = mask[i % 4] ^ pld[i];
    }
}

uint8_t *websocket_server::set_size_to_frame(uint32_t size, uint8_t *out_frame) {
    uint8_t *out_frame_ptr = out_frame;

    if (size < 126) {
        *out_frame_ptr = size;
        out_frame_ptr++;
    } else if (size < 65536) {
        out_frame_ptr[0] = 126;
        out_frame_ptr[1] = ((size >> 8) & 0xFF);
        out_frame_ptr[2] = (size & 0xFF);
        out_frame_ptr += 3;
    } else {
        out_frame_ptr[0] = 127;
        memset(&out_frame_ptr[1], 0, 6);
        out_frame_ptr[7] = ((size >> 8) & 0xFF);
        out_frame_ptr[8] = (size & 0xFF);
        out_frame_ptr += 9;
    }

    return out_frame_ptr;
}

uint8_t *websocket_server::set_data_to_frame(uint8_t *data, uint32_t size, uint8_t *out_frame) {
    memcpy(out_frame, data, size);
    return (out_frame + size);
}

void websocket_server::send_message(websocket_message *msg) {
    if (msg->msg_size + 10 > WS_SEND_BUFFER_SIZE) {
        lDebug(Warn, "Message too large for buffer");
        return;
    }

    uint8_t frame_buf[WS_SEND_BUFFER_SIZE];
    memset(frame_buf, 0, sizeof(frame_buf));
    frame_buf[0] = (uint8_t)msg->msg_type | WS_FIN_FLAG;
    uint8_t *p = set_size_to_frame(msg->msg_size, &frame_buf[1]);
    p = set_data_to_frame(msg->message, msg->msg_size, p);
    size_t frame_len = p - frame_buf;

    /* Snapshot active PCBs under mutex, then send outside mutex.
     * This avoids nesting LOCK_TCPIP_CORE inside the mutex. */
    struct altcp_pcb *targets[WS_MAX_CLIENTS] = {};
    int num_targets = 0;

    if (clients_mutex) xSemaphoreTake(clients_mutex, portMAX_DELAY);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (clients[i].established && clients[i].pcb) {
            targets[num_targets++] = clients[i].pcb;
        }
    }
    if (clients_mutex) xSemaphoreGive(clients_mutex);

    if (num_targets == 0) return;

    LOCK_TCPIP_CORE();
    for (int i = 0; i < num_targets; i++) {
        if (targets[i] == nullptr) continue;
        err_t err = altcp_write(targets[i], frame_buf, frame_len, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
            altcp_output(targets[i]);
        } else {
            lDebug(Warn, "altcp_write failed: %d", err);
        }
    }
    UNLOCK_TCPIP_CORE();
}

void websocket_server::handle_frame(websocket_client *c, uint8_t *buffer, int length) {
    /* Called from ws_recv_cb (lwIP core context, core lock already held).
     * Do NOT call LOCK_TCPIP_CORE() here — would deadlock. */
    if (length < 2)
        return;

    uint8_t len_byte = buffer[1] & 0x7F;
    int required = 2;
    if (len_byte == 126) {
        required = 4;
    } else if (len_byte == 127) {
        required = 10;
    }
    if (is_masked_msg(buffer)) {
        required += 4;
    }
    if (length < required)
        return;

    uint8_t *inbuf_ptr = buffer;

    if ((inbuf_ptr[0] & WS_TYPE_MASK) == WS_TYPE_PING) {
        lDebug(Info, "Received PING, sending PONG");
        uint8_t pong_frame[2] = { WS_FIN_FLAG | WS_TYPE_PONG, 0 };
        if (c->pcb) {
            altcp_write(c->pcb, pong_frame, 2, TCP_WRITE_FLAG_COPY);
            altcp_output(c->pcb);
        }
        return;
    }

    if ((inbuf_ptr[0] & WS_TYPE_MASK) == WS_TYPE_PONG) {
        lDebug(Info, "Received PONG");
        return;
    }

    if ((inbuf_ptr[0] & WS_TYPE_MASK) == WS_TYPE_CLOSE) {
        lDebug(Info, "Received CLOSE frame");
        uint8_t close_frame[2] = { WS_FIN_FLAG | WS_TYPE_CLOSE, 0 };
        if (c->pcb) {
            altcp_write(c->pcb, close_frame, 2, TCP_WRITE_FLAG_COPY);
            altcp_output(c->pcb);
            altcp_close(c->pcb);
        }
        c->established = false;
        c->pcb = nullptr;
        return;
    }

    if (is_fin_msg(inbuf_ptr)) {
        uint32_t len = get_message_len(inbuf_ptr);
        uint8_t *payload = get_payload_ptr(inbuf_ptr);

        if (is_masked_msg(inbuf_ptr)) {
            uint8_t *mask = get_mask(inbuf_ptr);
            unmask_message_payload(payload, len, mask);
        }

        if (msg_handler) {
            msg_handler(payload, len, (websocket_msg_type)(inbuf_ptr[0] & WS_TYPE_MASK));
        }
    }
}

// --- altcp callbacks ---

err_t websocket_server::ws_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    websocket_client *c = static_cast<websocket_client *>(arg);
    if (err != ERR_OK || p == nullptr || c == nullptr) {
        if (c) {
            c->established = false;
            c->pcb = nullptr;
        }
        if (p) pbuf_free(p);
        return ERR_OK;
    }

    uint16_t len = p->tot_len;
    if (len > WS_RECV_BUFFER_SIZE - 1) {
        len = WS_RECV_BUFFER_SIZE - 1;
    }
    pbuf_copy_partial(p, c->recv_buf, len, 0);
    c->recv_buf[len] = 0;

    altcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    if (c->server_ptr) {
        c->server_ptr->handle_frame(c, c->recv_buf, len);
    }

    return ERR_OK;
}

void websocket_server::ws_err_cb(void *arg, err_t err) {
    websocket_client *c = static_cast<websocket_client *>(arg);
    if (c) {
        lDebug(Info, "WebSocket client error %d, closing", err);
        c->established = false;
        c->pcb = nullptr;
    }
}

err_t websocket_server::ws_poll_cb(void *arg, struct altcp_pcb *pcb) {
    websocket_client *c = static_cast<websocket_client *>(arg);
    if (c == nullptr || !c->established) {
        return ERR_OK;
    }

    c->retries++;
    if (c->retries >= WS_MAX_POLL_RETRIES) {
        lDebug(Info, "WebSocket client timeout, closing");
        c->established = false;
        c->pcb = nullptr;
        altcp_close(pcb);
    }
    return ERR_OK;
}

err_t websocket_server::ws_sent_cb(void *arg, struct altcp_pcb *pcb, u16_t len) {
    websocket_client *c = static_cast<websocket_client *>(arg);
    if (c) {
        c->retries = 0;
    }
    return ERR_OK;
}

err_t websocket_server::ws_listener_accept_cb(void *arg, struct altcp_pcb *pcb, err_t err) {
    websocket_server *server = static_cast<websocket_server *>(arg);
    if (server == nullptr || pcb == nullptr || err != ERR_OK) {
        return ERR_VAL;
    }

    altcp_setprio(pcb, HTTPD_TCP_PRIO);
    altcp_arg(pcb, server);
    altcp_recv(pcb, ws_listener_recv_cb);
    altcp_err(pcb, ws_listener_err_cb);

    return ERR_OK;
}

err_t websocket_server::ws_listener_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    websocket_server *server = static_cast<websocket_server *>(arg);
    if (server == nullptr || pcb == nullptr || err != ERR_OK || p == nullptr) {
        if (p != nullptr) {
            altcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        return ERR_OK;
    }

    server->handle_altcp_connection(pcb, p);
    return ERR_OK;
}

void websocket_server::ws_listener_err_cb(void *arg, err_t err) {
    LWIP_UNUSED_ARG(arg);
    lDebug(Info, "WebSocket listener error %d", err);
}

// --- Client management ---

int websocket_server::alloc_client() {
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (!clients[i].established && clients[i].pcb == nullptr) {
            clients[i].server_ptr = this;
            return i;
        }
    }
    return -1;
}

void websocket_server::free_client(int idx) {
    if (idx >= 0 && idx < WS_MAX_CLIENTS) {
        clients[idx].established = false;
        clients[idx].pcb = nullptr;
        memset(clients[idx].recv_buf, 0, WS_RECV_BUFFER_SIZE);
        clients[idx].retries = 0;
    }
}

// --- Connection handler called from httpd ---

void websocket_server::handle_altcp_connection(struct altcp_pcb *pcb, struct pbuf *initial_data) {
    int idx = alloc_client();
    if (idx < 0) {
        lDebug(Warn, "No free WebSocket client slot, rejecting");
        altcp_abort(pcb);
        pbuf_free(initial_data);
        return;
    }

    websocket_client *c = &clients[idx];
    c->pcb = pcb;
    c->established = false;
    c->retries = 0;

    uint16_t len = initial_data->tot_len;
    if (len > WS_RECV_BUFFER_SIZE - 1) {
        len = WS_RECV_BUFFER_SIZE - 1;
    }
    pbuf_copy_partial(initial_data, c->recv_buf, len, 0);
    c->recv_buf[len] = 0;

    char *ws_key_accept = create_key_accept((char *)c->recv_buf);
    if (!ws_key_accept) {
        lDebug(Error, "Invalid WebSocket handshake request");
        altcp_abort(pcb);
        pbuf_free(initial_data);
        free_client(idx);
        return;
    }

    int written = snprintf((char *)send_buf, WS_SEND_BUFFER_SIZE, "%s%s\r\n\r\n", header, ws_key_accept);
    int response_len = (written < WS_SEND_BUFFER_SIZE) ? written : WS_SEND_BUFFER_SIZE - 1;

    err_t err = altcp_write(pcb, send_buf, response_len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        lDebug(Error, "Failed to send WebSocket handshake response");
        altcp_abort(pcb);
        pbuf_free(initial_data);
        free_client(idx);
        return;
    }
    altcp_output(pcb);

    altcp_recved(pcb, initial_data->tot_len);
    pbuf_free(initial_data);

    altcp_arg(pcb, c);
    altcp_recv(pcb, ws_recv_cb);
    altcp_err(pcb, ws_err_cb);
    altcp_poll(pcb, ws_poll_cb, WS_POLL_INTERVAL_MS / 500);
    altcp_sent(pcb, ws_sent_cb);

    c->established = true;
    lDebug(Info, "WebSocket client %d connected on port %u", idx, separate_listener_enabled ? WS_PORT : 80);
}

// --- FreeRTOS task ---

void websocket_server::task() {
    websocketQueue = xQueueCreate(10, sizeof(websocket_publish_message));

    clients_mutex = xSemaphoreCreateMutex();

    websocket_publish_message queued_msg;

    lDebug(Info,
           "WebSocket server task started (%s)",
           separate_listener_enabled ? "dedicated listener on port 8080" : "multiplexed on port 80 via httpd");

    while (1) {
        TickType_t now = xTaskGetTickCount();

        if (now - last_status_sent >= pdMS_TO_TICKS(500)) {
            last_status_sent = now;
            ArduinoJson::MyJsonDocument json = status_get();

            websocket_publish_message msg;
            size_t len = ArduinoJson::serializeJson(json, msg.payload, WS_MAX_PAYLOAD_LENGTH);
            msg.payload_length = len;

            if (xQueueSend(websocketQueue, &msg, 0) == pdPASS) {
                lDebug(Debug, "Status queued for broadcast");
            }
        }

        while (xQueueReceive(websocketQueue, &queued_msg, 0) == pdPASS) {
            websocket_message ws_msg;
            ws_msg.message = (uint8_t *)&queued_msg.payload;
            ws_msg.msg_size = queued_msg.payload_length;
            ws_msg.msg_type = WS_TYPE_STRING;
            send_message(&ws_msg);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void websocket_server::init(ws_callback_t callback, bool start_separate_listener) {
    msg_handler = callback;
    separate_listener_enabled = start_separate_listener;

    TaskHandle_t ws_serverTask_handle;
    xTaskCreate(
        [](void *ws) { static_cast<websocket_server *>(ws)->task(); },
        "ws_server",
        configMINIMAL_STACK_SIZE * 4,
        this,
        (configMAX_PRIORITIES - 1),
        &ws_serverTask_handle);

    if (separate_listener_enabled) {
        start_legacy_listener();
    }
}

void websocket_server::start_legacy_listener() {
    struct altcp_pcb *listener_pcb = altcp_tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (listener_pcb == nullptr) {
        lDebug(Error, "Failed to create WebSocket listener PCB");
        return;
    }

    altcp_setprio(listener_pcb, HTTPD_TCP_PRIO);
    err_t err = altcp_bind(listener_pcb, IP_ANY_TYPE, WS_PORT);
    if (err != ERR_OK) {
        lDebug(Error, "Failed to bind WebSocket listener to port %u: %d", WS_PORT, err);
        altcp_abort(listener_pcb);
        return;
    }

    struct altcp_pcb *listening_pcb = altcp_listen(listener_pcb);
    if (listening_pcb == nullptr) {
        lDebug(Error, "Failed to listen on WebSocket port %u", WS_PORT);
        altcp_abort(listener_pcb);
        return;
    }
    listener_pcb = listening_pcb;

    altcp_arg(listener_pcb, this);
    altcp_accept(listener_pcb, ws_listener_accept_cb);
    lDebug(Info, "WebSocket listener started on port %u", WS_PORT);
}
