#pragma once

#include "httpd.h"
#include "arduinojson_cust_alloc.h"

bool firmware_upload_is_request(const char *uri);
bool firmware_upload_begin(struct http_state *hs, const char *uri, int content_len, u8_t *post_auto_wnd);
err_t firmware_upload_receive(struct http_state *hs, struct pbuf *p, const char *uri);
void firmware_upload_finish(struct http_state *hs, const char *uri);
ArduinoJson::MyJsonDocument firmware_upload_status_json();
void firmware_install_if_pending();
