#pragma once
#include "FreeRTOS.h"
#include "semphr.h"
#include "stdarg.h"

inline xSemaphoreHandle s_PrintfSemaphore;

inline void debug_printf(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	xSemaphoreTake(s_PrintfSemaphore, portMAX_DELAY);
	vprintf(format, args);
	va_end(args);
	xSemaphoreGive(s_PrintfSemaphore);
}

inline void debug_write(const void *data, int size)
{
	xSemaphoreTake(s_PrintfSemaphore, portMAX_DELAY);
	for (int i = 0; i < size; i++) {
        putchar(((char *)data)[i]); // Send each character to the default UART
    }
	xSemaphoreGive(s_PrintfSemaphore);	

}
