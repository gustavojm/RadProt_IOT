#pragma once

#include "lwip/apps/mqtt_priv.h"
#include "FreeRTOS.h"
#include "queue.h"

inline QueueHandle_t mqttQueue;

// Define maximum lengths for the MQTT topic and payload
#define MAX_TOPIC_LENGTH 128
#define MAX_PAYLOAD_LENGTH 256

// Structure to hold MQTT publish information
typedef struct {
    char topic[MAX_TOPIC_LENGTH];      // MQTT topic to publish to
    char payload[MAX_PAYLOAD_LENGTH];  // Message payload to publish
    size_t payload_length;             // Length of the payload (in bytes)
    uint8_t qos;                       // Quality of Service (0, 1, or 2)
    bool retain;                       // Retain flag for the MQTT message
} MqttPublishMessage;

void mqtt_init();

// Example usage in FreeRTOS
int sendToMqttQueue(const char* topic, const char* payload, size_t payload_length, uint8_t qos, bool retain);
