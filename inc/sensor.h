#pragma once

#include "serial.h"
#include "mqtt.h"

class Sensor {
    public:

    Sensor(Serial uart, mqtt_client_t &mqtt_client) : uart(uart), mqtt_client(mqtt_client) {};
    void init();

    void read_serial_task();
    
    Serial uart;
    mqtt_client_t &mqtt_client;
};