
#include "serial.h"
#include "string.h"

// Function to handle UART IRQ
void Serial::on_uart_rx() {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (!receiving_task_handle) {
        return;
    }
    
    char c;
    if (uart_num < 2) {     // Hardware UARTS
        while (uart_is_readable(hardware_uart)) {
            c = uart_getc(hardware_uart);
            handle_received_char(c, xHigherPriorityTaskWoken);
        }
    } else {                // PIO UARTS
        while(!pio_sm_is_rx_fifo_empty(pio_hw, sm)) {   
            c = uart_rx_program_getc(pio_hw, sm);
            handle_received_char(c, xHigherPriorityTaskWoken);
        }    
    }        
}

void Serial::handle_received_char(char c, BaseType_t &xHigherPriorityTaskWoken) {
    portDISABLE_INTERRUPTS();

    if (timeout_detected) {
        uart_buffer->reset();
        string_finished_ = false;
        received_chars = 0;
        timeout_detected = false;
    } else {
        if (c == terminationChar || uart_buffer->space_left() == 1) {  // if we are about to overflow the buffer
            uart_buffer->push('\0');                               // Null-terminate the string
            string_finished_ = true;
            received_chars = 0;

            // Notification for read_string to indicate that a whole string was read or that the buffer is full
            vTaskNotifyGiveFromISR(receiving_task_handle, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        } else {
            uart_buffer->push(c);               // Add the character to the buffer      
            if  (received_chars++ == 1) {
                // Notification for read_string to indicate that a new string is being received
                vTaskNotifyGiveFromISR(receiving_task_handle, &xHigherPriorityTaskWoken);
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
        }
    }
    
    portENABLE_INTERRUPTS();
}

Serial::Serial(unsigned int uart_num, uint gpio_tx, uint gpio_rx, uint baud_rate, size_t uart_buffer_size)
    : uart_num(uart_num), gpio_tx(gpio_tx), gpio_rx(gpio_rx), baud_rate(baud_rate),
      uart_buffer(new RingBuffer<char>(uart_buffer_size)) {
        hardware_uart = uart_num == 0 ? uart0 : uart1;
        hardware_uart_IRQ = uart_num == 0 ? UART0_IRQ : UART1_IRQ;

        if (! uart_buffer) {
            lDebug(Error, "Serial Constructor, Out of Memory");
        }        

}

Serial::~Serial() {
    delete[] uart_buffer;
}

bool Serial::init(irq_handler_t handler) {
    // Initialize UART    
    uint pio_irq_index;
    pio_interrupt_source_t pis_sm_rx_fifo_not_empty;

    switch (uart_num) {
    case 0:
    case 1:       
        uart_init(hardware_uart, baud_rate);
        gpio_set_function(gpio_tx, GPIO_FUNC_UART);
        gpio_set_function(gpio_rx, GPIO_FUNC_UART);
    
        // Turn off FIFO's - we want to do this character by character
        uart_set_fifo_enabled(hardware_uart, false);    
            
        irq_set_enabled(hardware_uart_IRQ, true);
        uart_set_irq_enables(hardware_uart, true, false);
        irq_set_exclusive_handler(hardware_uart_IRQ, handler);
        return true;
        break;

    case 2:
    case 3:
        // Find a free pio
        pio_hw = pio1;
        if (!pio_can_add_program(pio_hw, &uart_rx_program)) {
            pio_hw = pio0;
            if (!pio_can_add_program(pio_hw, &uart_rx_program)) {
                offset = -1;
                panic("failed to setup pio");
                return false;
            }
        }
        offset = pio_add_program(pio_hw, &uart_rx_program);
        // Find a state machine
        sm = (int8_t)pio_claim_unused_sm(pio_hw, false);
        if (sm < 0) {
            panic("failed to setup pio");
            return false;
        }

        uart_rx_program_init(pio_hw, sm, offset, gpio_rx, baud_rate);

        // Find a free irq
        static_assert(PIO0_IRQ_1 == PIO0_IRQ_0 + 1 && PIO1_IRQ_1 == PIO1_IRQ_0 + 1, "");
        pio_irq = (pio_hw == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0;
        if (irq_get_exclusive_handler(pio_irq)) {
            pio_irq = static_cast<decltype(pio_irq)>(static_cast<int>(pio_irq) + 1);
            if (irq_get_exclusive_handler(pio_irq)) {
                panic("All IRQs are in use");
            }
        }
    
        // Enable interrupt
        irq_add_shared_handler(pio_irq, handler, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY); // Add a shared IRQ handler
        irq_set_enabled(pio_irq, true); // Enable the IRQ
        pio_irq_index = pio_irq - ((pio_hw == pio0) ? PIO0_IRQ_0 : PIO1_IRQ_0); // Get index of the IRQ
        
        pis_sm_rx_fifo_not_empty = pio_get_rx_fifo_not_empty_interrupt_source(sm);
        pio_set_irqn_source_enabled(pio_hw, pio_irq_index, pis_sm_rx_fifo_not_empty, true); // Set pio to tell us when the FIFO is NOT empty
    
        return true;
        break;

    default:
        return false;
        break;
    }

}

void Serial::set_receiving_task_handle() {
    receiving_task_handle = xTaskGetCurrentTaskHandle();
}

void Serial::set_receiving_task_handle(TaskHandle_t handle) {
    receiving_task_handle = handle;
}

int Serial::read_from_receive_buffer(char *buffer, size_t buffer_size) { 
    taskENTER_CRITICAL();
    char c;    
    size_t bytes = 0;
    while (uart_buffer->pop(c) && buffer_size-- > 1) {
        *buffer++ = c;
        bytes++;

        if (c == '\0') {
            break;
        }
    }
    string_finished_ = false;
    taskEXIT_CRITICAL();
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
            lDebug(Error, "Timeout");
            timeout_detected = true;
            return 0;
        }

        ulTaskNotifyTake(pdTRUE, xTicksToWait);
    }

    int bytes_read = read_from_receive_buffer(buffer, buffer_size);
    return bytes_read;
}
