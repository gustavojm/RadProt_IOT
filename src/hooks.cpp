#include "FreeRTOS.h"
#include "task.h"

#include "pico/printf.h"
#include "pico/stdio.h"

void vApplicationMallocFailedHook(void) { 
    printf("Malloc hook failed"); 
    while(1); // Stop here for debugging
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    printf("Stack overflow in %s", pcTaskName);
    while(1); // Stop here for debugging
}
