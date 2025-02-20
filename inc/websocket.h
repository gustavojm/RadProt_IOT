#pragma once

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/api.h"

#define WS_PORT                    8765
#define WS_MAX_CLIENTS             1
#define WS_SEND_BUFFER_SIZE        1024
#define WS_MSG_BUFFER_SIZE         512
#define WS_CLIENT_RECV_BUFFER_SIZE 1024

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11\0"

typedef enum { WS_TYPE_STRING = 0x81, WS_TYPE_BINARY = 0x82 } ws_type_t;

typedef struct {
    uint8_t *message;
    uint32_t msg_size;
    ws_type_t msg_type;
} ws_msg_t;

typedef struct {
    TaskHandle_t task_handle;
    struct netconn *accepted_sock;
    void *server_ptr;
    uint8_t recv_buf[WS_CLIENT_RECV_BUFFER_SIZE];
    uint32_t established;
} ws_client_t;

typedef struct {
    ws_client_t ws_clients[WS_MAX_CLIENTS];
    uint8_t send_buf[WS_SEND_BUFFER_SIZE];
    void (*msg_handler)(uint8_t *data, uint32_t len, ws_type_t type);
    uint32_t connected_clients_cnt = 0;
} ws_server_t;

inline ws_server_t ws_server;

void ws_server_init(ws_server_t *ws);
void ws_send_message(ws_server_t *ws, ws_msg_t *msg);