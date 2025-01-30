#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "hardware/uart.h"

class Serial {
public:
    Serial(uart_inst_t *uart, uint gpio_tx, uint gpio_rx, uint baud_rate, size_t uart_buffer_size);
    ~Serial();

    void init();
    int readString(char *buffer, size_t buffer_size, TickType_t timeout_ticks);
    void on_uart_rx();
    bool task_notified = false;

    static void set_irq_handler(uart_inst_t *uart_id, irq_handler_t handler);
    bool string_finished();
    int read_from_receive_buffer(char *buffer, size_t buffer_size);
    uart_inst_t *uart_id;
    
private:
    uint gpio_tx;
    uint gpio_rx;
    uint baud_rate;

    TickType_t timeout;
    volatile TaskHandle_t receiving_task_handle;
    char *uart_buffer;
    size_t uart_buffer_size;
    volatile size_t index;
    volatile bool string_finished_ = false;
};