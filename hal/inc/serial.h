#pragma once

#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "hardware/uart.h"
#include "hardware/pio.h"
#include "uart_rx.pio.h"
#include "debug.h"
#include "ringbuffer.h"

class Serial {
public:
    Serial(unsigned int uart_num, uint gpio_tx, uint gpio_rx, uint baud_rate, size_t uart_buffer_size);
    ~Serial() = default;

    // Delete the copy constructor
    Serial(const Serial&) = delete;

    // Delete the copy assignment operator
    Serial& operator=(const Serial&) = delete;

    bool init(irq_handler_t handler);
    int read_string(char *buffer, size_t buffer_size);
    void on_uart_rx();
    void handle_received_char(char c);
    bool task_notified = false;
    
    void set_timeout(TickType_t timeout);
    void set_receiving_task_handle();
    void set_receiving_task_handle(TaskHandle_t handle);

    int read_from_receive_buffer(char *buffer, size_t buffer_size);
    unsigned int uart_num;
    
private:
    uint gpio_tx;
    uint gpio_rx;
    uint baud_rate;

    uart_inst *hardware_uart;
    irq_num_t hardware_uart_IRQ;
    
    
    PIO pio_hw;    
    uint sm;
    irq_num_t pio_irq;
    uint offset;
    
    TickType_t timeout;
    volatile TaskHandle_t receiving_task_handle;
    RingBuffer<char> *uart_buffer;    
    volatile bool string_finished_ = false;
    volatile int received_chars = 0;
    volatile bool timeout_detected = false;
    TickType_t timeout_ticks = pdMS_TO_TICKS(1000); 
};