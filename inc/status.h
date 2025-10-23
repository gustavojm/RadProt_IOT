#pragma once

#include "MQTT.h"
#include <ArduinoJson.hpp>
#include "arduinojson_cust_alloc.h"

inline volatile bool initial_config = false;
inline volatile bool mqtt_reconnect = false;

ArduinoJson::MyJsonDocument status_get();
