#include "sensor.h"

#define SENSOR_READ_LED_GPIO 15

int Sensor::next_sensor_num = 0;

static void sensor_read_led_off(TimerHandle_t xTimer) {
    gpio_put(SENSOR_READ_LED_GPIO, false);
};

void Sensor::init() {
    TaskHandle_t sensor_task_handle;
    xTaskCreate([](void *me) { static_cast<Sensor *>(me)->read_task(); }, NULL, 2048, this, 1, &sensor_task_handle);
    uart.set_receiving_task_handle(sensor_task_handle);

    gpio_init(SENSOR_READ_LED_GPIO);
    gpio_set_dir(SENSOR_READ_LED_GPIO, true);

    sensor_read_led_off_timer = xTimerCreate(
        "",                     /* Text name for the software timer - not used by FreeRTOS. */
        pdMS_TO_TICKS(500),     /* How long will the led remain ON. */
        pdFALSE,                /* Setting uxAutoRealod to pdFALSE creates a one-shot software timer. */
        0,                      /* Timer id. */
        sensor_read_led_off     /* Callback function to be used by the software timer being created. */
    );

}

struct avg_fields_t {
    int avg_cnt_current;
    float accum;
};

void Sensor::sendToEndpoints(int sensor_num, int pub_setting_num, const char* name, const char *topic, const char *reading, size_t reading_length, uint8_t qos, bool retain) {
    gpio_put(SENSOR_READ_LED_GPIO, true);

    if (sendToMqttQueue(topic, reading, reading_length, qos, retain) != pdPASS) {
        printf("mqttQueue is full\n");
    }

    if (sendToWebsocketQueue(sensor_num, pub_setting_num, name, topic, reading, reading_length, qos, retain) != pdPASS) {
        printf("WebsocketQueue is full\n");
    }
    xTimerStart(sensor_read_led_off_timer, 0);

    
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
                    pub_settings.start < sizeof serial_buffer && pub_settings.end < sizeof serial_buffer) {
                    size_t len = pub_settings.end - pub_settings.start;


                    char *data = new char[len];
                    if (data != NULL) {
                        memcpy(data, &serial_buffer[pub_settings.start], len);
                        data[len] = '\0'; // Ensure null-termination

                        printf("****** %s ******\n", data);

                        if (pub_settings.is_num) {
                            errno = 0; /* To distinguish success/failure after call */
                            char *endptr;
                            float val = strtof(data, &endptr);
                            /* Check for various possible errors. */
                            if (errno != 0) {
                                printf("strtof");
                            }

                            if (endptr == data) {
                                printf("No digits were found in serial buffer: %s\n", serial_buffer);
                            }

                            /* If we got here, strtol() successfully parsed a number. */
                            // printf("strtof() returned %f\n", val);

                            val = val * pub_settings.scale;

                            if (pub_settings.avg_cnt > 0) {
                                avg_fields[i].accum += val;
                                avg_fields[i].avg_cnt_current++;

                                if (avg_fields[i].avg_cnt_current == pub_settings.avg_cnt) {
                                    float average = avg_fields[i].accum / pub_settings.avg_cnt;
                                    printf(
                                        "Publishing %s average: %f to: %s\n",
                                        pub_settings.name,
                                        average,
                                        pub_settings.topic);
                                    size_t len = snprintf(payload_buffer, sizeof payload_buffer, "%f", average);
                                    sendToEndpoints(sensor_num, i, pub_settings.name, pub_settings.topic, payload_buffer, len, 0, false);
                                    avg_fields[i].accum = 0;
                                    avg_fields[i].avg_cnt_current = 0;
                                }
                            } else {
                                printf("Publishing %s number: %f to: %s\n", pub_settings.name, val, pub_settings.topic);
                                size_t len = snprintf(payload_buffer, sizeof payload_buffer, "%f", val);
                                sendToEndpoints(sensor_num, i, pub_settings.name, pub_settings.topic, payload_buffer, len, 0, false);
                            }

                        } else {
                            printf("Publishing %s : %f to: %s:\n", pub_settings.name, data, pub_settings.topic);
                            sendToEndpoints(sensor_num, i, pub_settings.name, pub_settings.topic, data, strlen(data), 0, false);
                        }

                        delete[] data; // allocated by strndup
                    } else {
                        printf("strndup: Out of Memory\n");
                    }
                }
            }
        } else {
            printf("Read TIMED OUT\n");
        }
    }
}
