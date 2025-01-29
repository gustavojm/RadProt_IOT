/*
 * Copyright (c) 2017 Simon Goldschmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 *
 * Author: Simon Goldschmidt <goldsimon@gmx.de>
 *
 */

#include "lwip/opt.h"

#include "lwip/def.h"
#include "lwip/err.h"
#include "lwip/mem.h"

#include "httpd.h"
#include "httpd_structs.h"
#include <ArduinoJson.hpp>
#include <stdio.h>
#include <string.h>

#define USER_PASS_BUFSIZE 16

struct http_state *current_connection;

err_t httpd_post_begin(
    struct http_state *hs,
    const char *uri,
    const char *http_request,
    u16_t http_request_len,
    int content_len,
    char *response_uri,
    u16_t response_uri_len,
    u8_t *post_auto_wnd) {
    LWIP_UNUSED_ARG(http_request);
    LWIP_UNUSED_ARG(http_request_len);
    LWIP_UNUSED_ARG(content_len);
    LWIP_UNUSED_ARG(post_auto_wnd);

    current_connection = hs;
    return ERR_OK;  // return ERR_OK to parse received data
                    // return something else and set the response_uri and response_uri_len to return a file
}

err_t httpd_post_receive_data(struct http_state *hs, struct pbuf *p, const char *uri) {
    err_t ret;

    LWIP_ASSERT("NULL pbuf", p != NULL);

    struct pbuf *unique_pbuf = nullptr;
    if (p->next == NULL) { // Single pbuf?
        unique_pbuf = p;
    } else {
        unique_pbuf = pbuf_coalesce(p, PBUF_TRANSPORT);
        if (unique_pbuf == p) {
            printf("allocation failed");
            return -ENOMEM;
        }
    }
    
    if (uri && !memcmp(uri, "/settings.cgi", 14)) {
        namespace json = ArduinoJson;
        auto post_data = json::JsonDocument();
        json::DeserializationError error = json::deserializeJson(post_data, unique_pbuf->payload, unique_pbuf->len);

        if (error) {
            printf("Error json parse. %s", error.c_str());
        } else {
            char const *ssid = post_data["ssid"];
            printf("SSID: %s", ssid);
        }
        ret = ERR_OK;
    }
    /* this function must ALWAYS free the pbuf it is passed or it will leak memory */
    pbuf_free(unique_pbuf);

    return ret;
}

void httpd_post_finished(struct http_state *hs, char *response_uri, u16_t response_uri_len) {
  static char body[]="{\"message\": \"Hello, world!\"}";
  
    if (hs == current_connection) {
        hs->hdrs[HDR_STRINGS_IDX_HTTP_STATUS] = g_psHTTPHeaderStrings[HTTP_HDR_OK];
        hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN_KEEPALIVE] = g_psHTTPHeaderStrings[HTTP_HDR_CONTENT_LENGTH];
        hs->hdrs[HDR_STRINGS_IDX_CONTENT_TYPE] = HTTP_HDR_JSON;
        
        hs->file = body;
        hs->left = sizeof body;

        size_t len;
        lwip_itoa(hs->hdr_content_len, (size_t)LWIP_HTTPD_MAX_CONTENT_LEN_SIZE, hs->left);
        len = strlen(hs->hdr_content_len);
        if (len <= LWIP_HTTPD_MAX_CONTENT_LEN_SIZE - LWIP_HTTPD_MAX_CONTENT_LEN_OFFSET) {
            SMEMCPY(&hs->hdr_content_len[len], CRLF, 3);
            hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN_NR] = hs->hdr_content_len;
        }
    #if LWIP_HTTPD_SUPPORT_11_KEEPALIVE
        if (add_content_len) {
            hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN_KEEPALIVE] = g_psHTTPHeaderStrings[HTTP_HDR_KEEPALIVE_LEN];
        } else {
            hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN_KEEPALIVE] = g_psHTTPHeaderStrings[HTTP_HDR_CONN_CLOSE];
            hs->keepalive = 0;
        }
    #else  /* LWIP_HTTPD_SUPPORT_11_KEEPALIVE */
        hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN_KEEPALIVE] = g_psHTTPHeaderStrings[HTTP_HDR_CONTENT_LENGTH];
    #endif /* LWIP_HTTPD_SUPPORT_11_KEEPALIVE */

    } else {
        hs->hdrs[HDR_STRINGS_IDX_HTTP_STATUS] = g_psHTTPHeaderStrings[HTTP_HDR_BAD_REQUEST];
        hs->hdrs[HDR_STRINGS_IDX_CONTENT_LEN_KEEPALIVE] = g_psHTTPHeaderStrings[HTTP_HDR_CONN_CLOSE];
    }
    /* Set up to send the first header string. */
    hs->hdr_index = 0;
    hs->hdr_pos = 0;

}
