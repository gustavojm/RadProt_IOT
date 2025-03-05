#pragma once

#include "serial.h"
#include "string.h"
#include "settings.h"

#include "MQTT.h"
#include "websocket.h"

#include "timers.h"

class Sensor {
    public:

    Sensor(Serial &uart) : uart(uart), settings(settings) {
        assert(next_sensor_num < MAX_SERIAL_SENSORS);
        
        sensor_num = next_sensor_num++;
        settings = &(get_client_settings()->sensor_settings)[sensor_num];
    };

    void init();

    void read_task();
    
    void sendToEndpoints(int sensor_num, int pub_setting_num, const char* name, const char *topic, const char *reading, size_t reading_length, uint8_t qos, bool retain);
    
    Serial &uart;    
    const sensor_settings_entry *settings;
    TimerHandle_t sensor_read_led_off_timer;
    int sensor_num = 0;

    static int next_sensor_num;
    
};
