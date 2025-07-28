/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "settings.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "MQTT.h"
#include "queue.h"
#include "task.h"

#include "FreeRTOS/MQTTFreeRTOS.h"

#include "MQTTClient.h"
#include "websocket.h"
#include "status.h"

void messageArrived(MessageData *data) {
    lDebug(Info, 
        "Message arrived on topic %.*s: %.*s",
        data->topicName->lenstring.len,
        data->topicName->lenstring.data,
        data->message->payloadlen,
        data->message->payload);
}

static void status_led_off(TimerHandle_t xTimer) {
    gpio_put(STATUS_LED_GPIO, false);
};

static void mqtt_task(void *pvParameters) {
    MQTTClient client;
    Network network;
    unsigned char sendbuf[80], readbuf[80];
    int rc = 0;

    pvParameters = 0;
    NetworkInit(&network);
    MQTTClientInit(&client, &network, 3000, sendbuf, sizeof(sendbuf), readbuf, sizeof(readbuf));

    if ((rc = MQTTStartTask(&client)) != pdPASS) {
        lDebug(Error, "Error MQTT start tasks: %d", rc);
    }

    const client_mode_settings *client_settings = get_client_mode_settings();

    while (true) {        
        if ((rc = NetworkConnectWithTimeout(&network, client_settings->mqtt.broker, client_settings->mqtt.port, 1000)) == 0) {

            MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;
            connectData.MQTTVersion = 3;

            connectData.clientID.cstring = const_cast<char *>("RadProt_IOT");
            connectData.username.cstring = const_cast<char *>(client_settings->mqtt.username);
            connectData.password.cstring = const_cast<char *>(client_settings->mqtt.password);

            if ((rc = MQTTConnect(&client, &connectData)) == 0) {
                lDebug(Info, "MQTT Connected");
                mqtt_connection_status = true;

                // if ((rc = MQTTSubscribe(&client, "FreeRTOS/sample/#", QOS0, messageArrived)) != 0) {
                //     lDebug(Error, "Error MQTT subscribe: %d", rc);
                // }

                MqttPublishMessage msg;

                while (true) {
                    if (xQueueReceive(mqttQueue, &msg, pdMS_TO_TICKS(500)) == pdPASS) {
                        // Publish the message using your MQTT client library
                        MQTTMessage message;

                        message.qos = (enum QoS)msg.qos;
                        message.retained = 0;
                        message.payload = msg.payload;
                        message.payloadlen = strlen(msg.payload);

                        if ((rc = MQTTPublish(&client, msg.topic, &message)) == 0) {
                            gpio_put(STATUS_LED_GPIO, true);
                            xTimerStart(status_led_off_timer, 0);
                            lDebug(Info, "--MQTT-->");
                        } else {
                            lDebug(Error, "Error publishing: %d", rc);
                            goto close_socket;
                            // break;
                        }
                    } else {
                        if (mqtt_reconnect) {
                            mqtt_reconnect = false;
                            goto close_socket;
                        }
                    }
                }
            } else {
                lDebug(Error, "Error connecting: %d", rc);
                mqtt_connection_status = false;
            }
        } else {
            lDebug(Error, "Error in network connection: %d", rc);
            mqtt_connection_status = false;
        }
    close_socket:
        network.disconnect(&network);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
    /* do not return */
}

int sendToMqttQueue(const char *topic, const char *payload, size_t payload_length, uint8_t qos, bool retain) {
    if (mqttQueue) {
        MqttPublishMessage msg;

        // Copy topic and payload into the structure
        snprintf(msg.topic, MQTT_MAX_TOPIC_LENGTH, "%s", topic);
        snprintf(msg.payload, MQTT_MAX_PAYLOAD_LENGTH, "%s", payload);
        msg.payload_length = payload_length;
        msg.qos = qos;
        msg.retain = retain;

        // Send the message to the FreeRTOS queue
        int ret = xQueueSend(mqttQueue, &msg, 0);
        return ret;
    }
    return pdPASS;
}

void mqtt_init() {
    // Create a queue to hold MQTT publish messages
    mqttQueue = xQueueCreate(10, sizeof(MqttPublishMessage)); // 10 is the queue size
    if (mqttQueue != NULL) {
        xTaskCreate(
            mqtt_task,                  // Task to be run
            "MQTTTask",                 // Name of the Task for debugging and managing its Task Handle
            configMINIMAL_STACK_SIZE,   // Stack depth to be allocated for use with task's stack (see docs)
            NULL,                       // Arguments needed by the Task (NULL because we don't have any)
            (configMAX_PRIORITIES - 2), // Task Priority - Higher the number the more priority [max is (configMAX_PRIORITIES
                                        // - 1) provided in FreeRTOSConfig.h]
            NULL                        // Task Handle if available for managing the task
        );

        status_led_off_timer = xTimerCreate(
            "",                 /* Text name for the software timer - not used by FreeRTOS. */
            pdMS_TO_TICKS(500), /* How long will the led remain ON. */
            pdFALSE,            /* Setting uxAutoRealod to pdFALSE creates a one-shot software timer. */
            0,                  /* Timer id. */
            status_led_off      /* Callback function to be used by the software timer being created. */
        );
    }
}
