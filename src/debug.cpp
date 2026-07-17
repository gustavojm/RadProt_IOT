#include <stdio.h>

#include "debug.h"

#if !defined(NDEBUG)

/**
 * @brief 	sets debug level.
 * @param 	lvl 	:name of file to send output to
 */
void debugSetLevel(enum debugLevels lvl) {
    debugLevel = lvl;
}

void debugWrite(const void *data, int size) {
	xSemaphoreTake(s_PrintfSemaphore, portMAX_DELAY);
    for (int i = 0; i < size; i++) {
        putchar(reinterpret_cast<const char *>(data)[i]); // Send each character to the default UART
    }
    xSemaphoreGive(s_PrintfSemaphore);
}

#endif // !defined(NDEBUG)
