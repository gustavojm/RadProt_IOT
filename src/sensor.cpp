#include "sensor.h"

void Sensor::init() {
    TaskHandle_t sensor_task_handle;
    xTaskCreate([](void *me) { static_cast<Sensor *>(me)->read_task(); }, NULL, 1024, this, 1, &sensor_task_handle);
    uart.set_receiving_task_handle(sensor_task_handle);
}

struct avg_fields_t{
    int avg_cnt_current;
    float accum;
};

void Sensor::read_task() {
    char serial_buffer[SERIAL_BUFFERS_SIZE];    
    char payload_buffer[SERIAL_BUFFERS_SIZE] = {};
    avg_fields_t avg_fields[MAX_PUBLISH_SETTINGS] {};

    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int bytes_received = uart.read_string(serial_buffer, sizeof(serial_buffer));
       
        if (bytes_received) { // If we received something
            for (int i = 0; i < MAX_PUBLISH_SETTINGS; i++) {
                const publish_settings_entry &pub_settings = settings->publish_settings[i];
                if (pub_settings.enabled && pub_settings.end >= pub_settings.start && pub_settings.start < sizeof serial_buffer && pub_settings.end < sizeof serial_buffer) {
                    size_t len = pub_settings.end - pub_settings.start;
                    char *data = strndup(&serial_buffer[pub_settings.start], len);
                    if (data) {
                        // printf("****** %s ******\n", data);
                    
                        if (pub_settings.is_num) {                  
                            errno = 0;    /* To distinguish success/failure after call */
                            char *endptr;
                            float val = strtof(data, &endptr);
                            /* Check for various possible errors. */
                            if (errno != 0) {
                                printf("strtof");                     
                            }

                            if (endptr == data) {
                                printf("No digits were found\n");                        
                            }

                            /* If we got here, strtol() successfully parsed a number. */
                            // printf("strtof() returned %f\n", val);

                            val = val * pub_settings.scale;                        

                            if (pub_settings.avg_cnt > 0) {
                                avg_fields[i].accum += val;
                                avg_fields[i].avg_cnt_current++;

                                if (avg_fields[i].avg_cnt_current == pub_settings.avg_cnt) {
                                    float average = avg_fields[i].accum / pub_settings.avg_cnt;
                                    printf("Publishing %s average: %f to: %s\n", pub_settings.name, average, pub_settings.topic);
                                    size_t len = snprintf(payload_buffer, sizeof payload_buffer, "%f", average);
                                    sendToMqttQueue(pub_settings.topic, payload_buffer, len, 0, false);
                                    avg_fields[i].accum = 0;
                                    avg_fields[i].avg_cnt_current = 0;
                                }
                            } else {
                                printf("Publishing %s number: %f to: %s\n", pub_settings.name, val, pub_settings.topic);
                                size_t len = snprintf(payload_buffer, sizeof payload_buffer, "%f", val);
                                sendToMqttQueue(pub_settings.topic, payload_buffer, len, 0, false);
                            }

                        } else {
                            printf("Publishing %s : %f to: %s:\n", pub_settings.name, data, pub_settings.topic);
                            sendToMqttQueue(pub_settings.topic, data, strlen(data), 0, false);
                        }

                        free(data); // allocated by strndup
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
