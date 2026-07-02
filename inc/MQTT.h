#pragma once

#include "lwip/apps/mqtt_priv.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "timers.h"
#include "hardware/flash.h"
#include "pico/flash.h"

#include "debug.h"

inline QueueHandle_t mqttQueue;
inline TimerHandle_t status_led_off_timer;
inline char client_id[32];

inline volatile bool mqtt_connection_status = false;

// Define maximum lengths for the MQTT topic and payload
#define MQTT_MAX_TOPIC_LENGTH 128
#define MQTT_MAX_PAYLOAD_LENGTH 256

// Structure to hold MQTT publish information
typedef struct {
    char topic[MQTT_MAX_TOPIC_LENGTH];      // MQTT topic to publish to
    char payload[MQTT_MAX_PAYLOAD_LENGTH];  // Message payload to publish
    size_t payload_length;             // Length of the payload (in bytes)
    uint8_t qos;                       // Quality of Service (0, 1, or 2)
    bool retain;                       // Retain flag for the MQTT message
} MqttPublishMessage;

void __not_in_flash_func(get_unique_id)(void *param);

void mqtt_init();

int sendToMqttQueue(const char* topic, const char* payload, size_t payload_length, uint8_t qos, bool retain);
