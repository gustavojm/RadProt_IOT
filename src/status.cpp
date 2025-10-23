
#include "status.h"
#include "settings.h"

ArduinoJson::MyJsonDocument status_get() {
    ArduinoJson::MyJsonDocument json;

    size_t mem_free = xPortGetFreeHeapSize();
    size_t mem_min_free = xPortGetMinimumEverFreeHeapSize();
    
    json["mem"]["total"] = configTOTAL_HEAP_SIZE;
    json["mem"]["free"] = mem_free;
    json["mem"]["min_free"] = mem_min_free;

    json["mqtt"]["connected"] = mqtt_connection_status;
    json["button"] = !gpio_get(INITIAL_CONFIG_GPIO);

    return json;
}
