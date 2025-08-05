#include "sensor.h"

int Sensor::next_sensor_num = 0;

void Sensor::init() {
    TaskHandle_t sensor_task_handle;
    xTaskCreate(
        [](void *me) { static_cast<Sensor *>(me)->read_task(); },
        NULL,
        configMINIMAL_STACK_SIZE,
        this,
        1,
        &sensor_task_handle);
    uart.set_receiving_task_handle(sensor_task_handle);
}

void Sensor::sendToEndpoints(
    int sensor_num,
    int pub_setting_num,
    const char *name,
    const char *topic,
    const char *value,
    size_t value_length,
    uint8_t qos,
    bool retain) {
    if (sendToMqttQueue(topic, value, value_length, qos, retain) != pdPASS) {
        lDebug(Warn, "mqttQueue is full");
    }

    ArduinoJson::MyJsonDocument json;

    json["reading"]["s_s"] = sensor_num;
    json["reading"]["p_s"] = pub_setting_num;
    json["reading"]["p_s_name"] = name;
    json["reading"]["value"] = value;
    json["reading"]["topic"] = topic;

    websocket_publish_message msg;
    size_t len = ArduinoJson::serializeJson(json, msg.payload, WS_MAX_PAYLOAD_LENGTH);
    msg.payload_length = len;

    if (websocketQueue) {
        if (xQueueSend(websocketQueue, &msg, 0) != pdPASS) {
            lDebug(Warn, "WebsocketQueue is full");
        }
    }
};

void Sensor::process_and_publish(const char *data, int index) {
    const publish_settings_entry &pub_settings = settings->publish_settings[index];    
    char payload_buffer[SERIAL_BUFFERS_SIZE] = {};

    if (pub_settings.is_num) {
        errno = 0;
        char *endptr;
        float val = strtof(data, &endptr);

        if (errno != 0)
            lDebug(Error, "strtof");

        if (endptr == data)
            lDebug(Warn, "No digits were found in serial buffer: %s", data);

        val *= pub_settings.scale;

        if (pub_settings.avg_cnt > 0) {
            avg_fields[index].accum += val;
            avg_fields[index].avg_cnt_current++;

            if (avg_fields[index].avg_cnt_current == pub_settings.avg_cnt) {
                float average = avg_fields[index].accum / pub_settings.avg_cnt;
                lDebug(Info, "Publishing %s average: %f to: %s", pub_settings.name, average, pub_settings.topic);
                size_t len = snprintf(payload_buffer, sizeof payload_buffer, "%f", average);
                sendToEndpoints(sensor_num, index, pub_settings.name, pub_settings.topic, payload_buffer, len, 1, false);
                avg_fields[index].accum = 0;
                avg_fields[index].avg_cnt_current = 0;
            }
        } else {
            lDebug(Info, "Publishing %s value: %f to: %s", pub_settings.name, val, pub_settings.topic);
            size_t len = snprintf(payload_buffer, sizeof payload_buffer, "%f", val);
            sendToEndpoints(sensor_num, index, pub_settings.name, pub_settings.topic, payload_buffer, len, 1, false);
        }
    } else {
        lDebug(Info, "Publishing %s value: %s to: %s", pub_settings.name, data, pub_settings.topic);
        sendToEndpoints(sensor_num, index, pub_settings.name, pub_settings.topic, data, strlen(data), 1, false);
    }
}

void Sensor::read_task() {
    while (true) {
        char serial_buffer[SERIAL_BUFFERS_SIZE] = {'\0'};

        if (settings->simulate_values) {
            for (int i = 0; i < MAX_PUBLISH_SETTINGS; i++) {
                const publish_settings_entry &pub_settings = settings->publish_settings[i];
                if (!pub_settings.enabled || pub_settings.end <= pub_settings.start ||
                    pub_settings.start >= SERIAL_BUFFERS_SIZE || pub_settings.end > SERIAL_BUFFERS_SIZE)
                    continue;

                size_t len = pub_settings.end - pub_settings.start;
                if (len >= SERIAL_BUFFERS_SIZE)
                    len = SERIAL_BUFFERS_SIZE - 1;

                char data[SERIAL_BUFFERS_SIZE]{};
                int random_num = rand() % 10;
                memset(data, '0' + random_num, len);
                data[len] = '\0';

                lDebug(Info, "****** %s ****** sensor %i: **", data, sensor_num);
                process_and_publish(data, i);
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            int bytes_received = uart.read_string(serial_buffer, sizeof(serial_buffer));

            if (bytes_received) {
                for (int i = 0; i < MAX_PUBLISH_SETTINGS; i++) {
                    const publish_settings_entry &pub_settings = settings->publish_settings[i];
                    if (!pub_settings.enabled || pub_settings.end <= pub_settings.start ||
                        pub_settings.start >= bytes_received)
                        continue;

                    size_t len;
                    if (pub_settings.end > bytes_received) {
                        len = bytes_received - pub_settings.start;
                    } else {
                        len = pub_settings.end - pub_settings.start;
                    }      
                    
                    if (len == 0)
                        continue;

                    if (len > SERIAL_BUFFERS_SIZE - 1)
                        len = SERIAL_BUFFERS_SIZE - 1;

                    char data[SERIAL_BUFFERS_SIZE]{};
                    memcpy(data, &serial_buffer[pub_settings.start], len);
                    data[len] = '\0';

                    lDebug(Info, "****** %s ****** sensor %i: **", data, sensor_num);
                    process_and_publish(data, i);
                }
            } else {
                lDebug(Warn, "Read TIMED OUT");
            }
        }
    }
}
