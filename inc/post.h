#pragma once

#include "httpd.h"
#include "errno.h"
#include "arduinojson_cust_alloc.h"

typedef ArduinoJson::MyJsonDocument (*handler_ptr)(struct http_state *hs);

typedef struct {
    const char *handler_name;
    handler_ptr handler_function;
} post_handler_entry;

bool apply_settings_from_json(ArduinoJson::MyJsonDocument &json, ArduinoJson::MyJsonDocument &responseJson, bool skip_password_check = false);
ArduinoJson::MyJsonDocument get_settings_backup_json();

