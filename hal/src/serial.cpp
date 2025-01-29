
#include "serial.h"

extern TaskHandle_t readTaskHandle;

// Function to handle UART IRQ
void Serial::on_uart_rx() {
    while (uart_is_readable(uart_id)) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;

        if (readTaskHandle != NULL && !task_notified) {
            /* Send notification from ISR */
            vTaskNotifyGiveFromISR(readTaskHandle, &xHigherPriorityTaskWoken);
            task_notified = true;
            /* Request a context switch if needed */
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }        
        char c = uart_getc(uart_id);
        xQueueSendFromISR(uartQueue, &c, NULL); // Send received char to the queue
    }
}

Serial::Serial(uart_inst_t *uart, uint gpio_tx, uint gpio_rx, uint baud_rate)
    : uart_id(uart), gpio_tx(gpio_tx), gpio_rx(gpio_rx), baud_rate(baud_rate) {
}

void Serial::init() {
    // Initialize UART
    uart_init(uart_id, baud_rate);
    gpio_set_function(gpio_tx, GPIO_FUNC_UART);
    gpio_set_function(gpio_rx, GPIO_FUNC_UART);

    // Set up a queue for UART data
    uartQueue = xQueueCreate(queue_size, sizeof(char));
    if (uartQueue == NULL) {
        printf("Failed to create UART queue\n");
        while (1)
            ;
    }
}

void Serial::enable_irq(uart_inst_t *uart_id, irq_handler_t handler) {
    // Set up UART IRQ
    irq_num_t IRQ = uart_id == uart0 ? UART0_IRQ : UART1_IRQ;

    irq_set_exclusive_handler(IRQ, handler);
    irq_set_enabled(IRQ, true);
    uart_set_irq_enables(uart_id, true, false);
}

// Function to read a string with a timeout
void Serial::readString(char *buffer, size_t buffer_size, TickType_t timeout_ticks) {
    size_t index = 0;
    const char terminationChar = '\n';
    char receivedChar;

    TickType_t start_time = xTaskGetTickCount(); // Record the start time

    while (1) {
        // Wait for a character from the queue with a timeout
        if (xQueueReceive(uartQueue, &receivedChar, timeout_ticks) == pdTRUE) {
            if (receivedChar == terminationChar) {
                buffer[index] = '\0'; // Null-terminate the string
                task_notified = false;
                return;               // Exit after receiving the termination character
            } else if (index < buffer_size - 1) {
                buffer[index++] = receivedChar; // Add the character to the buffer
            }
        }

        // Check if the timeout has elapsed
        if ((xTaskGetTickCount() - start_time) > timeout_ticks) {
            buffer[index] = '\0'; // Null-terminate and return the partial string
            task_notified = false;
            return;
        }
    }
}
