#include "websocket.h"
#include "MQTT.h"

const char *head_ws = "HTTP/1.1 101 Switching Protocols\n\
Upgrade: websocket\n\
Connection: Upgrade\nSec-WebSocket-Accept: \0";

static char *get_ws_key(char *buf, size_t *len) {
    char *p = strstr(buf, "Sec-WebSocket-Key: ");
    if (p) {
        p = p + strlen("Sec-WebSocket-Key: ");
        *len = strchr(p, '\r') - p;
    }
    return p;
}

static char *create_ws_key_accept(char *inbuf) {
    static char concat_key[64] = { 0 };
    static char hash[22] = { 0 };
    static char hash_base64[64] = { 0 };
    size_t len = 0, baselen = 0;

    char *key = get_ws_key(inbuf, &len);
    strncpy(concat_key, key, len);
    strcat(concat_key, WS_GUID);
    mbedtls_sha1((uint8_t *)concat_key, 60, (uint8_t *)hash);
    mbedtls_base64_encode((uint8_t *)hash_base64, 64, &baselen, (uint8_t *)hash, 20);    
    return hash_base64;
}

static uint32_t get_message_len(uint8_t *msg) {
    uint32_t len = 0;

    if ((msg[1] & 0x7F) == 126)
        len = msg[2] | msg[3];
    else
        len = msg[1] & 0x7F;

    return len;
}

static bool is_masked_msg(uint8_t *msg) {
    return (msg[1] & WS_MASKED_FLAG);
}

static bool is_fin_msg(uint8_t *msg) {
    return (msg[0] & WS_FIN_FLAG);
}

static uint8_t *get_mask(uint8_t *msg) {
    if ((msg[1] & 0x7F) == 126)
        return &msg[4];
    else
        return &msg[2];
}

static uint8_t *get_payload_ptr(uint8_t *msg) {
    uint8_t *p = ((msg[1] & 0x7F) == 126) ? (msg + 4) : (msg + 2);
    if (is_masked_msg(msg))
        p += 4;
    return p;
}

static void unmask_message_payload(uint8_t *pld, uint32_t len, uint8_t *mask) {
    for (int i = 0; i < len; i++)
        pld[i] = mask[i % 4] ^ pld[i];
}

static uint8_t *ws_set_size_to_frame(uint32_t size, uint8_t *out_frame) {
    uint8_t *out_frame_ptr = out_frame;
    if (size < 126) {
        *out_frame_ptr = size;
        out_frame_ptr++;
    } else {
        out_frame_ptr[0] = 126;
        out_frame_ptr[1] = ((((uint16_t)size) >> 8) & 0xFF);
        out_frame_ptr[2] = (((uint16_t)size) & 0xFF);
        out_frame_ptr += 3;
    }
    return out_frame_ptr;
}

static uint8_t *ws_set_data_to_frame(uint8_t *data, uint8_t size, uint8_t *out_frame) {
    memcpy(out_frame, data, size);
    return (out_frame + size);
}

void ws_send_message(ws_server_t *ws, ws_msg_t *msg) {
    uint8_t *outbuf_ptr = ws->send_buf;
    ws_client_t *client;

    if (msg->msg_size + 7 > WS_SEND_BUFFER_SIZE)
        return;

    memset(outbuf_ptr, 0x00, WS_SEND_BUFFER_SIZE);
    outbuf_ptr[0] = (uint8_t)msg->msg_type | WS_FIN_FLAG;
    outbuf_ptr = ws_set_size_to_frame(msg->msg_size, &outbuf_ptr[1]);
    outbuf_ptr = ws_set_data_to_frame(msg->message, msg->msg_size, outbuf_ptr);
    size_t packet_size = outbuf_ptr - ws->send_buf;

    for (int iClient = 0; iClient < WS_MAX_CLIENTS; iClient++) {
        client = &(ws->ws_clients[iClient]);
        if (client->established) {
            // Set send timeout using setsockopt
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 500000; // 500 ms
            lwip_setsockopt(client->socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            
            int bytes_sent = lwip_send(client->socket, ws->send_buf, packet_size, 0);
            if (bytes_sent < 0) {
                lDebug(Error, "Write failed with err %d (\"%s\")", errno, strerror(errno));
            }
        }
    }
}

static void ws_client_task(void *arg) {
    ws_client_t *client = (ws_client_t *)arg;
    ws_server_t *server_ptr = client->server_ptr;
    
    while (true) {
        // The created task is in standby mode
        // until an incoming connection unblocks it
        vTaskSuspend(NULL);

        int recv_bytes;
        while ((recv_bytes = lwip_recv(client->socket, client->recv_buf, WS_RECV_BUFFER_SIZE, 0)) > 0) {
            uint8_t *inbuf_ptr = client->recv_buf;

            // If is handshake
            if (strncmp((char *)inbuf_ptr, "GET /", 5) == 0) {
                char *ws_key_accept = create_ws_key_accept((char *)inbuf_ptr);
                sprintf((char *)server_ptr->send_buf, "%s%s%s", head_ws, ws_key_accept, "\r\n\r\n");
                lwip_send(client->socket, server_ptr->send_buf, strlen((char *)server_ptr->send_buf), 0);
            }
            // If is a message
            else if (is_fin_msg(inbuf_ptr)) {
                if ((inbuf_ptr[0] & WS_TYPE_MASK) == WS_TYPE_PING) {
                    lDebug(Info, "Websocket PING");
                }

                if ((inbuf_ptr[0] & WS_TYPE_MASK) == WS_TYPE_PONG) {
                    lDebug(Info, "Websocket PONG");
                }

                if ((inbuf_ptr[0] & WS_TYPE_MASK) == WS_TYPE_CLOSE) {
                    lDebug(Info, "Websocket CLOSE");
                    break;
                }

                uint32_t len = get_message_len(inbuf_ptr);
                uint8_t *payload = get_payload_ptr(inbuf_ptr);
                if (is_masked_msg(inbuf_ptr)) {
                    uint8_t *mask = get_mask(inbuf_ptr);
                    unmask_message_payload(payload, len, mask);
                }
                server_ptr->msg_handler(payload, len, (ws_type_t)inbuf_ptr[0]);
            }
            
            // Clear buffer for next read
            memset(client->recv_buf, 0, WS_RECV_BUFFER_SIZE);
        }
        
        lDebug(Error, "Receive failed with err %d (\"%s\") closing socket", errno, strerror(errno));

        client->established = false;
        lwip_close(client->socket);
    }
}

static void ws_create_clients_tasks(ws_server_t *ws) {
    ws_client_t *client;

    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        client = &ws->ws_clients[i];        
        client->server_ptr = ws;
        xTaskCreate(ws_client_task, "ws_client", 256, (void *)client, (tskIDLE_PRIORITY + 2), &client->task_handle);
    }
}

/**
 * Function of starting a websocket server_ptr and receiving incoming connections.
 * When the stream of receiving messages is free,
 * the structure of the received connection is passed to it.
 *
 * @param arg ws_server_ptr structure (refer websockets.h)
 */
void ws_server_task(void *arg) {
    WebsocketPublishMessage msg;
    websocketQueue = xQueueCreate(10, sizeof(WebsocketPublishMessage)); // 10 is the queue size

    ws_server_t *ws = (ws_server_t *)arg;
    ws_client_t *client;

    // Create server socket
    int server_sock = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock < 0) {
        lDebug(Error, "Failed to create socket");
        vTaskDelete(NULL);
    }
       
    // Prepare server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(WS_PORT);
    
    // Bind socket
    if (lwip_bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        lDebug(Error, "Socket bind failed");
        lwip_close(server_sock);
        vTaskDelete(NULL);
    }
    
    // Listen for connections
    if (lwip_listen(server_sock, WS_MAX_CLIENTS) < 0) {
        lDebug(Error, "Listen failed");
        lwip_close(server_sock);
        vTaskDelete(NULL);
    }
    
    memset((void *)ws->send_buf, 0x00, WS_SEND_BUFFER_SIZE);

    ws_create_clients_tasks(ws);
    
    // Set up timeout for accept
    struct timeval timeout;
    fd_set readfds;

    while (true) {
        for (int iClient = 0; iClient < WS_MAX_CLIENTS; iClient++) {
            client = &ws->ws_clients[iClient];
            if (!client->established) {
                // Set timeout for accept
                FD_ZERO(&readfds);
                FD_SET(server_sock, &readfds);
                timeout.tv_sec = 0;
                timeout.tv_usec = 100000; // 100 ms
                
                // Check for connection with timeout
                int activity = lwip_select(server_sock + 1, &readfds, NULL, NULL, &timeout);
               
                if (activity > 0 && FD_ISSET(server_sock, &readfds)) {
                    struct sockaddr_in client_addr;
                    socklen_t addr_len = sizeof(client_addr);
                    
                    // Accept new connection
                    int client_sock = lwip_accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
                    
                    if (client_sock >= 0) {
                        // Store socket in client structure
                        client->socket = client_sock;
                        client->established = true;
                        
                        // Resume the task that will handle the processing
                        vTaskResume(client->task_handle);
                    }
                }
            }
        }

        while (xQueueReceive(websocketQueue, &msg, 100) == pdPASS) {
            ws_msg_t ws_msg;
            ws_msg.message = (uint8_t *) &msg.payload;
            ws_msg.msg_size = msg.payload_length;
            ws_msg.msg_type = WS_TYPE_STRING;
            ws_send_message(ws, &ws_msg);
            lDebug(Info, "---WS--->");
        }        
    }
}

void ws_server_init(ws_server_t *ws) {    
    TaskHandle_t ws_serverTask_handle;
    xTaskCreate(ws_server_task, "ws_server", configMINIMAL_STACK_SIZE, (void *)ws, (configMAX_PRIORITIES - 1), &ws_serverTask_handle);
    vTaskCoreAffinitySet(ws_serverTask_handle, 1);
}