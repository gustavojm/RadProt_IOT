#include "sensor.h"

void Sensor::init() {
    xTaskCreate([](void *me) { static_cast<Sensor *>(me)->read_serial_task(); }, NULL, 1024, this, 1, NULL);
}

void Sensor::read_serial_task() {
    char buffer[128];

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int bytes_received = uart.read_string(buffer, sizeof(buffer));
        if (bytes_received) { // If we received something
            printf("Received string: %.*s\n", bytes_received, buffer);



        } else {
            printf("Read TIMED OUT");
        }
    }
}

