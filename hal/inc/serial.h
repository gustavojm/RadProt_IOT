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

    // Delete the copy constructor
    Serial(const Serial&) = delete;

    // Delete the copy assignment operator
    Serial& operator=(const Serial&) = delete;

    void init(irq_handler_t handler);
    int read_string(char *buffer, size_t buffer_size);
    void on_uart_rx();
    bool task_notified = false;
    
    void set_timeout(TickType_t timeout);
    void set_delimiter(char delimiter);
    void set_receiving_task_handle();
    void set_receiving_task_handle(TaskHandle_t handle);

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
    TickType_t timeout_ticks = pdMS_TO_TICKS(1000); 
    char terminationChar = '\n';
};