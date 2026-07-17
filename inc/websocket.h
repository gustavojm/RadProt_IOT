#pragma once

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "debug.h"

#include <ArduinoJson.hpp>
#include "arduinojson_cust_alloc.h"

#include "status.h"
#include "httpd_opts.h"

#include "lwip/altcp.h"
#include "lwip/pbuf.h"
#include "lwip/tcpip.h"
#include "lwip/sockets.h"

#include <string.h>
#include "crypto.h"

constexpr char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr int WS_FIN_FLAG = 1 << 7;
constexpr int WS_MASKED_FLAG = 1 << 7;
constexpr int WS_TYPE_MASK = 0xF;
constexpr int WS_TYPE_TEXT = 0x01;
constexpr int WS_TYPE_BINARY = 0x02;
constexpr int WS_TYPE_CLOSE = 0x08;
constexpr int WS_TYPE_PING = 0x09;
constexpr int WS_TYPE_PONG = 0x0A;

constexpr int WS_MAX_CLIENTS = 2;
constexpr int WS_SEND_BUFFER_SIZE = 512;
constexpr int WS_RECV_BUFFER_SIZE = 512;
constexpr int WS_MAX_PAYLOAD_LENGTH = 512;
constexpr int WS_POLL_INTERVAL_MS = 2000;
constexpr int WS_MAX_POLL_RETRIES = 4;
constexpr uint16_t WS_PORT = 8080;

inline QueueHandle_t websocketQueue;

enum websocket_msg_type {
    WS_TYPE_STRING = WS_TYPE_TEXT,
    WS_TYPE_BIN = WS_TYPE_BINARY
};

struct websocket_message {
    uint8_t *message;
    uint32_t msg_size;
    websocket_msg_type msg_type;
};

struct websocket_publish_message {
    char payload[WS_SEND_BUFFER_SIZE];
    uint32_t payload_length;
};

typedef void (*ws_callback_t)(uint8_t *payload, uint32_t length, websocket_msg_type type);

class websocket_server;

struct websocket_client{
    struct altcp_pcb *pcb = nullptr;
    bool established = false;
    uint8_t recv_buf[WS_RECV_BUFFER_SIZE];
    int retries = 0;
    websocket_server *server_ptr = nullptr;
};

class websocket_server {
    char send_buf[WS_SEND_BUFFER_SIZE] = {};
    websocket_client clients[WS_MAX_CLIENTS] = {};
    ws_callback_t msg_handler = nullptr;
    TickType_t last_status_sent = 0;
    SemaphoreHandle_t clients_mutex = nullptr;
    bool separate_listener_enabled = false;

private:
    void task();
    int alloc_client();
    void free_client(int idx);
    void send_message(websocket_message *msg);
    void handle_frame(websocket_client *c, uint8_t *buffer, int length);
    char *create_key_accept(char *inbuf);
    char *get_key(char *buf, size_t *len);
    uint32_t get_message_len(uint8_t *msg);
    bool is_masked_msg(uint8_t *msg);
    bool is_fin_msg(uint8_t *msg);
    uint8_t *get_mask(uint8_t *msg);
    uint8_t *get_payload_ptr(uint8_t *msg);
    void unmask_message_payload(uint8_t *pld, uint32_t len, uint8_t *mask);
    uint8_t *set_size_to_frame(uint32_t size, uint8_t *out_frame);
    uint8_t *set_data_to_frame(uint8_t *data, uint32_t size, uint8_t *out_frame);

    static err_t ws_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err);
    static void   ws_err_cb(void *arg, err_t err);
    static err_t ws_poll_cb(void *arg, struct altcp_pcb *pcb);
    static err_t ws_sent_cb(void *arg, struct altcp_pcb *pcb, u16_t len);
    static err_t ws_listener_accept_cb(void *arg, struct altcp_pcb *pcb, err_t err);
    static err_t ws_listener_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err);
    static void   ws_listener_err_cb(void *arg, err_t err);

public:
    void init(ws_callback_t callback, bool start_separate_listener = false);
    void handle_altcp_connection(struct altcp_pcb *pcb, struct pbuf *initial_data);
    void start_legacy_listener();
};

inline websocket_server ws_server;
