#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "hardware/uart.h"

class Serial {
public:
    Serial(uart_inst_t *uart, uint gpio_tx, uint gpio_rx, uint baud_rate);
    void init();
    void readString(char *buffer, size_t buffer_size, TickType_t timeout_ticks);
    void on_uart_rx();
    bool task_notified = false;

    static void enable_irq(uart_inst_t *uart_id, irq_handler_t handler);
private:
    QueueHandle_t uartQueue;
    uart_inst_t *uart_id;
    uint gpio_tx;
    uint gpio_rx;
    uint baud_rate;

    TickType_t timeout;
    int queue_size = 128;
};