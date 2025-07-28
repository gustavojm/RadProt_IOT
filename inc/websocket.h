#pragma once

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "debug.h"

#include <ArduinoJson.hpp>
#include "arduinojson_cust_alloc.h"

#include "lwip/api.h"

#include <string.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include "status.h"
#include "crypto.h"

#define WS_PORT                 8765
#define WS_GUID                 "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_FIN_FLAG             1 << 7
#define WS_MASKED_FLAG          1 << 7
#define WS_TYPE_MASK            0xF
#define WS_TYPE_TEXT            0x01
#define WS_TYPE_BINARY          0x02
#define WS_TYPE_CLOSE           0x08
#define WS_TYPE_PING            0x09
#define WS_TYPE_PONG            0x0A
#define SELECT_TIMEOUT_MS       100

#define WS_MAX_CLIENTS          1
#define WS_SEND_BUFFER_SIZE     512
#define WS_RECV_BUFFER_SIZE     512

#define WS_MAX_PAYLOAD_LENGTH   512

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

// Forward declaration
class websocket_server;

struct websocket_client{
    int socket = -1;
    bool established = false;
    uint8_t recv_buf[WS_RECV_BUFFER_SIZE];
    websocket_server *server_ptr =  nullptr;
};

class websocket_server {
    uint8_t send_buf[WS_SEND_BUFFER_SIZE] = {};
    websocket_client client = {}; 
    ws_callback_t msg_handler = nullptr;
    TickType_t last_status_sent = 0;

private:
    void task();
    void send_message(websocket_message *msg);
    void handle_frame(uint8_t *buffer, int length);
    bool process_handshake(uint8_t *buffer);
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

public:
    void init(ws_callback_t callback);    
};

inline websocket_server ws_server;