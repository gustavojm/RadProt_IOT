
#include "status.h"

ArduinoJson::MyJsonDocument status_get() {
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

    return json;
}