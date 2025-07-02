#include "sensor.h"

int Sensor::next_sensor_num = 0;

void Sensor::init() {
    TaskHandle_t sensor_task_handle;
    xTaskCreate([](void *me) { static_cast<Sensor *>(me)->read_task(); }, NULL, 1024, this, 1, &sensor_task_handle);
    uart.set_receiving_task_handle(sensor_task_handle);
}

struct avg_fields_t {
    int avg_cnt_current;
    float accum;
};

void Sensor::sendToEndpoints(int sensor_num, int pub_setting_num, const char* name, const char *topic, const char *reading, size_t reading_length, uint8_t qos, bool retain) {
    if (sendToMqttQueue(topic, reading, reading_length, qos, retain) != pdPASS) {
        lDebug(Warn, "mqttQueue is full");
    }

    ArduinoJson::MyJsonDocument json;

    static size_t old_mem_free;
    static size_t old_mem_min_free;
    
    size_t mem_free = xPortGetFreeHeapSize();
    size_t mem_min_free = xPortGetMinimumEverFreeHeapSize();
    
    if (old_mem_free != mem_free || old_mem_min_free != mem_min_free) {
        json["mem"]["total"] = configTOTAL_HEAP_SIZE;
        json["mem"]["free"] = mem_free;
        json["mem"]["min_free"] = mem_min_free;
    }       
    old_mem_free = mem_free;
    old_mem_min_free = mem_min_free;

    json["mqtt"]["connected"] = mqtt_connection_status;

    json["s_s"] = sensor_num;
    json["p_s"] = pub_setting_num;
    json["p_s_name"] = name;
    json["reading"] = reading;
    json["topic"] = topic;

    websocket_publish_message msg;
    size_t len = ArduinoJson::serializeJson(json, msg.payload, WS_MAX_PAYLOAD_LENGTH);
    msg.payload_length = len;
    
    if (websocketQueue) {
        if (xQueueSend(websocketQueue, &msg, 0) != pdPASS) {
            lDebug(Warn, "WebsocketQueue is full");
        }      
    }
};

void Sensor::read_task() {
    char serial_buffer[SERIAL_BUFFERS_SIZE];
    char payload_buffer[SERIAL_BUFFERS_SIZE] = {};
    avg_fields_t avg_fields[MAX_PUBLISH_SETTINGS]{};

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int bytes_received = uart.read_string(serial_buffer, sizeof(serial_buffer));

        if (bytes_received) { // If we received something
            for (int i = 0; i < MAX_PUBLISH_SETTINGS; i++) {
                const publish_settings_entry &pub_settings = settings->publish_settings[i];
                if (pub_settings.enabled && pub_settings.end >= pub_settings.start &&
                    pub_settings.start < bytes_received && pub_settings.end < bytes_received) {
                    size_t len = pub_settings.end - pub_settings.start;

                    char data[SERIAL_BUFFERS_SIZE] {};
                    memcpy(data, &serial_buffer[pub_settings.start], len);
                    data[len] = '\0'; // Ensure null-termination

                    lDebug(Info, "****** %s ****** sensor %i: **", data, sensor_num);

                    if (pub_settings.is_num) {
                        errno = 0; /* To distinguish success/failure after call */
                        char *endptr;
                        float val = strtof(data, &endptr);
                        /* Check for various possible errors. */
                        if (errno != 0) {
                            lDebug(Error, "strtof");
                        }

                        if (endptr == data) {
                            lDebug(Warn, "No digits were found in serial buffer: %s", serial_buffer);
                            // lDebug(Warn, "No digits were found in serial buffer");
                        }

                        /* If we got here, strtol() successfully parsed a number. */
                        // lDebug(Info, "strtof() returned %f", val);

                        val = val * pub_settings.scale;

                        if (pub_settings.avg_cnt > 0) {
                            avg_fields[i].accum += val;
                            avg_fields[i].avg_cnt_current++;

                            if (avg_fields[i].avg_cnt_current == pub_settings.avg_cnt) {
                                float average = avg_fields[i].accum / pub_settings.avg_cnt;
                                lDebug(Info, 
                                    "Publishing %s average: %f to: %s",
                                    pub_settings.name,
                                    average,
                                    pub_settings.topic);
                                size_t len = snprintf(payload_buffer, sizeof payload_buffer, "%f", average);
                                sendToEndpoints(sensor_num, i, pub_settings.name, pub_settings.topic, payload_buffer, len, 1, false);
                                avg_fields[i].accum = 0;
                                avg_fields[i].avg_cnt_current = 0;
                            }
                        } else {
                            lDebug(Info, "Publishing %s value: %f to: %s", pub_settings.name, val, pub_settings.topic);
                            size_t len = snprintf(payload_buffer, sizeof payload_buffer, "%f", val);
                            sendToEndpoints(sensor_num, i, pub_settings.name, pub_settings.topic, payload_buffer, len, 1, false);
                        }

                    } else {
                        lDebug(Info, "Publishing %s value: %s to: %s:", pub_settings.name, data, pub_settings.topic);
                        sendToEndpoints(sensor_num, i, pub_settings.name, pub_settings.topic, data, strlen(data), 1, false);
                    }
                }
            }
        } else {
            lDebug(Warn, "Read TIMED OUT");
        }
    }
}
