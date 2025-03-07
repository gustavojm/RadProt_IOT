#pragma once

#include "httpd.h"
#include "errno.h"
#include "arduinojson_cust_alloc.h"

typedef ArduinoJson::MyJsonDocument (*handler_ptr)(struct http_state *hs);

typedef struct {
    const char *handler_name;
    handler_ptr handler_function;
} post_handler_entry;

