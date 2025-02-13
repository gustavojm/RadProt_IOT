/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "mqtt.h"
#include "queue.h"
#include "task.h"

#include "FreeRTOS/MQTTFreeRTOS.h"

#include "MQTTClient.h"

void messageArrived(MessageData *data) {
    printf(
        "Message arrived on topic %.*s: %.*s\n",
        data->topicName->lenstring.len,
        data->topicName->lenstring.data,
        data->message->payloadlen,
        data->message->payload);
}

static void mqtt_task(void *pvParameters) {
    /* connect to m2m.eclipse.org, subscribe to a topic, send and receive messages regularly every 1 sec */
    MQTTClient client;
    Network network;
    unsigned char sendbuf[80], readbuf[80];
    int rc = 0;
    int count = 0;
    MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;

    pvParameters = 0;
    NetworkInit(&network);
    MQTTClientInit(&client, &network, 30000, sendbuf, sizeof(sendbuf), readbuf, sizeof(readbuf));

    char address[] = "192.168.137.243";
    // char address[] = "test.mosquitto.org";
    // char address[] = "5.196.78.28";

    if ((rc = NetworkConnect(&network, address, 1883)) != 0) {
        printf("Error in network connection: %d\n", rc);
    }

    connectData.MQTTVersion = 3;
    char clientID[] = "FreeRTOS_sample";
    connectData.clientID.cstring = clientID;

    if ((rc = MQTTConnect(&client, &connectData)) != 0) {
        printf("Error connecting: %d\n", rc);
    } else {
        printf("MQTT Connected\n");
    }
        
    // if ((rc = MQTTSubscribe(&client, "FreeRTOS/sample/#", QOS2, messageArrived)) != 0)
    // 	printf("Return code from MQTT subscribe is %d\n", rc);

    // if ((rc = MQTTStartTask(&client)) != pdPASS)
    // printf("Return code from start tasks is %d\n", rc);

    MqttPublishMessage msg;

    while (true) {
        if (xQueueReceive(mqttQueue, &msg, portMAX_DELAY) == pdPASS) {
            // Publish the message using your MQTT client library
            MQTTMessage message;

            message.qos = (enum QoS)msg.qos;
            message.retained = 0;
            message.payload = msg.payload;
            message.payloadlen = strlen(msg.payload);

            if ((rc = MQTTPublish(&client, msg.topic, &message)) != 0) {
                printf("Error publishing: %d\n", rc);
            }
        }
    }

    /* do not return */
}

void sendToMqttQueue(const char *topic, const char *payload, size_t payload_length, uint8_t qos, bool retain) {
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

void mqtt_init() {
    // Create a queue to hold MQTT publish messages
    mqttQueue = xQueueCreate(10, sizeof(MqttPublishMessage)); // 10 is the queue size
    if (mqttQueue != NULL) {
        xTaskCreate(
            mqtt_task,                  // Task to be run
            "MQTTTask",                 // Name of the Task for debugging and managing its Task Handle
            1024,                       // Stack depth to be allocated for use with task's stack (see docs)
            NULL,                       // Arguments needed by the Task (NULL because we don't have any)
            (configMAX_PRIORITIES - 2), // Task Priority - Higher the number the more priority [max is (configMAX_PRIORITIES
                                        // - 1) provided in FreeRTOSConfig.h]
            NULL // Task Handle if available for managing the task
        );
    }
}
