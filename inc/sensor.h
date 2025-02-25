#pragma once

#include "serial.h"
#include "string.h"
#include "settings.h"

#include "MQTT.h"
#include "websocket.h"

#include "timers.h"

class Sensor {
    public:

    Sensor(Serial &uart, const sensor_settings_entry *settings) : uart(uart), settings(settings) {};
    void init();

    void read_task();

    void sendToEndpoints(const char* topic, const char* payload, size_t payload_length, uint8_t qos, bool retain);
    
    Serial &uart;    
    const sensor_settings_entry *settings;
    TimerHandle_t sensor_read_led_off_timer;
};
