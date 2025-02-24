#pragma once

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "lwip/api.h"

#define WS_PORT                    8765
#define WS_MAX_CLIENTS             5
#define WS_SEND_BUFFER_SIZE        1024
#define WS_MSG_BUFFER_SIZE         512
#define WS_CLIENT_RECV_BUFFER_SIZE 1024
#define WS_FIN_FLAG                1 << 7
#define WS_MASKED_FLAG             1 << 7
#define WS_TYPE_MASK               0xF

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11\0"

inline QueueHandle_t websocketQueue;

typedef enum { 
    WS_TYPE_CONT = 0x0, 
    WS_TYPE_STRING = 0x1, 
    WS_TYPE_BINARY = 0x2,
    WS_TYPE_CLOSE = 0x8,
    WS_TYPE_PING = 0x9,
    WS_TYPE_PONG = 0xA,
} ws_type_t;

typedef struct {
    uint8_t *message;
    uint32_t msg_size;
    ws_type_t msg_type;
} ws_msg_t;

typedef struct ws_client {
    TaskHandle_t task_handle;
    volatile struct netconn *accepted_sock;
    void *server_ptr;
    uint8_t recv_buf[WS_CLIENT_RECV_BUFFER_SIZE] = {};
    volatile bool established = false;
} ws_client_t;

typedef struct {
    ws_client_t ws_clients[WS_MAX_CLIENTS];
    uint8_t send_buf[WS_SEND_BUFFER_SIZE] = {};
    void (*msg_handler)(uint8_t *data, uint32_t len, ws_type_t type);
} ws_server_t;

void ws_server_init(ws_server_t *ws);
void ws_send_message(ws_server_t *ws, ws_msg_t *msg);
int sendToWebsocketQueue(ws_msg_t msg);