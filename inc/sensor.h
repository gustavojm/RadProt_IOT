#pragma once

#include "serial.h"
#include "mqtt.h"
#include "string.h"
#include "settings.h"

class Sensor {
    public:

    Sensor(Serial &uart, const sensor_settings_entry *settings) : uart(uart), settings(settings) {};
    void init();

    void read_task();
    
    Serial &uart;    
    const sensor_settings_entry *settings;
    
};
