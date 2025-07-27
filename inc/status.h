#pragma once

#include "MQTT.h"
#include <ArduinoJson.hpp>
#include "arduinojson_cust_alloc.h"

inline bool mqtt_reconnect = false;

ArduinoJson::MyJsonDocument status_get();