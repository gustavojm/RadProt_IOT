#include "FreeRTOS.h"
#include "task.h"
#include "cstring"
#include "lwip/dns.h"
#include "settings.h"

#include "mqtt.h"

#include <pico/cyw43_arch.h>

#define DEBUG_printf printf

// #define MQTT_SERVER_HOST "telemetria"
#define MQTT_SERVER_PORT 1883

#define MS_PUBLISH_PERIOD 5000
#define MQTT_TLS          0 // needs to be 1 for AWS IoT

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    if (status != 0) {
        DEBUG_printf("Error during connection: err %d.\n", status);
    } else {
        DEBUG_printf("MQTT connected.\n");
    }
}

void sendToMqttQueue(const char* topic, const char* payload, size_t payload_length, uint8_t qos, bool retain) {
    MqttPublishMessage msg;

    // Copy topic and payload into the structure
    snprintf(msg.topic, MAX_TOPIC_LENGTH, "%s", topic);
    snprintf(msg.payload, MAX_PAYLOAD_LENGTH, "%s", payload);
    msg.payload_length = payload_length;
    msg.qos = qos;
    msg.retain = retain;

    // Send the message to the FreeRTOS queue
    if (xQueueSend(mqttQueue, &msg, portMAX_DELAY) != pdPASS) {
        // Handle error: failed to send to the queue
    }
}

/*
 * Called when publish is complete either with success or failure
 */
static void mqtt_pub_request_cb(void *arg, err_t err) {
    // mqtt_client_t *client = static_cast<mqtt_client_t *>(arg);
    // printf("request publish ERROR %d \n", err);
}

/* Initiate client and connect to server, if this fails immediately an error code is returned
 * otherwise mqtt_connection_cb will be called with connection result after attempting to
 * to establish a connection with the server. For now MQTT version 3.1.1 is always used
 */
err_t mqtt_app_connect(mqtt_client_t *client, ip_addr_t broker_addr) {
    const client_settings *client_settings = get_client_settings();


    mqtt_connect_client_info_t ci{};
    ci.client_id = "PicoW";
    
    if (strlen(client_settings->mqtt.username)==0) {
        ci.client_user = NULL;
    } else {
        ci.client_user = client_settings->mqtt.username;
    }
    
    if (strlen(client_settings->mqtt.password)==0) {
        ci.client_pass = NULL;
    } else {
        ci.client_pass = client_settings->mqtt.password;
    }

    ci.keep_alive = 0;
    ci.will_topic = "/rad-prot/wills";
    ci.will_msg = "Connection Down";
    ci.will_retain = 0;
    ci.will_qos = 2;

    err_t err = mqtt_client_connect(client, &broker_addr, MQTT_SERVER_PORT, mqtt_connection_cb, client, &ci);

    if (err != ERR_OK) {
        DEBUG_printf("ERROR en mqtt_app_connect() %d \n", err);
    }

    return err;
}

void dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    char *ip_addr = ip4addr_ntoa(ipaddr);
    printf("RESOLVED HOSTNAME TO: %s\n", ip_addr);
}

void mqtt_task(void *pvParameters) {
    MqttPublishMessage msg;
    DEBUG_printf("Inicio mqtt_connection_task\n");
    ip_addr_t broker_addr;
    //IP4_ADDR(&broker_addr, 192, 168, 137, 243);
    //IP4_ADDR(&broker_addr, 10, 30, 113, 111);   // Windows Machine acting as ssh tunnel to mqtt server

    //IP4_ADDR(&broker_addr, 5, 196, 78, 28);  // test.mosquitto.org
    int counter = 0;
    mqtt_client_t mqtt_client{};

    const client_settings *client_settings = get_client_settings();
    dns_gethostbyname(client_settings->mqtt.broker_address, &broker_addr, dns_found_cb, NULL);

    //vTaskDelay(10000);

    err_t error = mqtt_app_connect(&mqtt_client, broker_addr);
    if (error == ERR_OK) {
        DEBUG_printf("mqtt_app_connect() OK\n");
    } else {
        DEBUG_printf("mqtt_app_connect() ERROR=%d\n", error);
    }  

    while (true) {
        if (!mqtt_client_is_connected(&mqtt_client)) {
            mqtt_app_connect(&mqtt_client, broker_addr);
        }
        // Wait for a message to arrive in the queue
        if (xQueueReceive(mqttQueue, &msg, portMAX_DELAY) == pdPASS) {
            // Publish the message using your MQTT client library
            mqtt_publish(&mqtt_client, msg.topic, msg.payload, msg.payload_length, msg.qos, msg.retain, mqtt_pub_request_cb, NULL);
            printf("****PUBLISH****\n");
        }    
    }
}

void mqtt_init() {
    // Create a queue to hold MQTT publish messages
    mqttQueue = xQueueCreate(10, sizeof(MqttPublishMessage)); // 10 is the queue size
    if (mqttQueue != NULL) {    
        xTaskCreate(
            mqtt_task,                  // Task to be run
            "MQTTTask",                 // Name of the Task for debugging and managing its Task Handle
            1024,                       // Stack depth to be allocated for use with task's stack (see docs)
            NULL,                    // Arguments needed by the Task (NULL because we don't have any)
            (configMAX_PRIORITIES - 2), // Task Priority - Higher the number the more priority [max is (configMAX_PRIORITIES - 1)
                                        // provided in FreeRTOSConfig.h]
            NULL                        // Task Handle if available for managing the task
        );
    }
}