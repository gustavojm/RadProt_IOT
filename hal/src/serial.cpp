
#include "serial.h"
#include "string.h"

// Function to handle UART IRQ
void Serial::on_uart_rx() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (!receiving_task_handle) {
        return;
    }
    
    while (uart_is_readable(uart_id)) {
        // Notification for task to indicate that a uart reception has started. The task will start the reception with a deadline
        vTaskNotifyGiveFromISR(receiving_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);

        char c = uart_getc(uart_id);
        if (c == terminationChar || index == (uart_buffer_size - 2)) { // if we are about to overflow the buffer
            uart_buffer[index++] = '\0';                                 // Null-terminate the string
            string_finished_ = true;
            // Notification for read_string to indicate that a whole string was read or that the buffer is full
            vTaskNotifyGiveFromISR(receiving_task_handle, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        } else if (index < uart_buffer_size - 1) {
            uart_buffer[index++] = c; // Add the character to the buffer
        }
    }
}

Serial::Serial(uart_inst_t *uart, uint gpio_tx, uint gpio_rx, uint baud_rate, size_t uart_buffer_size)
    : uart_id(uart), gpio_tx(gpio_tx), gpio_rx(gpio_rx), baud_rate(baud_rate), uart_buffer_size(uart_buffer_size),
      uart_buffer(new char[uart_buffer_size]) {
        if (! uart_buffer) {
            printf("Serial Constructor, Out of Memory\n");
        }        
}

Serial::~Serial() {
    delete[] uart_buffer;
}

void Serial::init(irq_handler_t handler) {
    // Initialize UART
    uart_init(uart_id, baud_rate);
    gpio_set_function(gpio_tx, GPIO_FUNC_UART);
    gpio_set_function(gpio_rx, GPIO_FUNC_UART);

    // Turn off FIFO's - we want to do this character by character
    uart_set_fifo_enabled(uart_id, false);    

    irq_num_t IRQ = uart_id == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_enabled(IRQ, true);
    uart_set_irq_enables(uart_id, true, false);
    irq_set_exclusive_handler(IRQ, handler);

}

void Serial::set_receiving_task_handle() {
    receiving_task_handle = xTaskGetCurrentTaskHandle();
}

void Serial::set_receiving_task_handle(TaskHandle_t handle) {
    receiving_task_handle = handle;
}

int Serial::read_from_receive_buffer(char *buffer, size_t buffer_size) {
    vTaskEnterCritical();
    size_t bytes = (index < buffer_size) ? index : buffer_size;
    memcpy(buffer, uart_buffer, bytes);
    memset(uart_buffer, '\0', uart_buffer_size);
    index = 0;
    string_finished_ = false;
    vTaskExitCritical();
    return bytes;
}

void Serial::set_timeout(TickType_t timeout) {
    timeout_ticks = timeout;
}

void Serial::set_delimiter(char delimiter) {
    terminationChar = delimiter;
}

// Function to read a string with a timeout
int Serial::read_string(char *buffer, size_t buffer_size) {

    TimeOut_t xTimeOut;
    TickType_t xTicksToWait = timeout_ticks;
    /* Initialize xTimeOut. This records the time at which this function was entered. */
    vTaskSetTimeOutState(&xTimeOut);

    while (!string_finished_) {
        if (xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait) != pdFALSE) {
            /* Timed out before the whole string was received, exit the loop. */
            break;
        }

        ulTaskNotifyTake(pdTRUE, timeout_ticks);
    }
    return read_from_receive_buffer(buffer, buffer_size);
}
