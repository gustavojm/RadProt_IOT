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
#include <ssi.h>
#include <stdio.h>
#include <string.h>
#include <settings.h>

namespace json = ArduinoJson;

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
        auto post_data = json::JsonDocument();
        json::DeserializationError error = json::deserializeJson(post_data, unique_pbuf->payload, unique_pbuf->len);

        if (error) {
            printf("Error json parse. %s", error.c_str());
        } else {
            /* Extracting data from form */
            char const *ssid = post_data["ssid"];
        }
        ret = ERR_OK;

        auto body_JSON = json::JsonDocument();

        body_JSON["algunakey"] = "algunvalor";
        char *body = nullptr;
        int body_len = 0;
        body_len = json::measureJson(body_JSON); /* returns 0 on fail */
        body = new char[body_len];
        if (!(*body)) {
            printf("Out Of Memory");
            body_len = 0;
        } else {
            json::serializeJson(body_JSON, body, body_len);
        }
        httpd_post_response(hs, body, body_len, "json"); // indicate JSON IMPROVE THIS
    }

    if (uri && !memcmp(uri, "/wifi_nets.cgi", 15)) {
        auto body_JSON = json::JsonDocument();
        auto wifi_nets_array = body_JSON["WIFI_NETS"].to<json::JsonArray>();

        for (auto wifi_net : wifi_networks) {
            auto wifi_net_entry = json::JsonDocument();
            wifi_net_entry["ssid"] = wifi_net.ssid;
            wifi_net_entry["rssi"] = wifi_net.rssi;
            wifi_net_entry["chann"] = wifi_net.channel;
            wifi_net_entry["auth_mode"] = wifi_net.auth_mode;

            char bssid[18];
            snprintf(bssid, sizeof bssid,
                "%02x:%02x:%02x:%02x:%02x:%02x",
                wifi_net.bssid[0],
                wifi_net.bssid[1],
                wifi_net.bssid[2],
                wifi_net.bssid[3],
                wifi_net.bssid[4],
                wifi_net.bssid[5]);
            wifi_net_entry["bssid"] = bssid;
            

            wifi_nets_array.add(wifi_net_entry);
        }
        
        //body_JSON["config"] = get_client_settings_json();

        char *body = nullptr;
        int body_len = 0;
        body_len = json::measureJson(body_JSON); /* returns 0 on fail */
        body = new char[body_len];
        if (!(*body)) {
            printf("Out Of Memory");
            body_len = 0;
        } else {
            json::serializeJson(body_JSON, body, body_len);
        }
        httpd_post_response(hs, body, body_len, "json"); //
    }

    if (uri && !memcmp(uri, "/config_get.cgi", 16)) {
        auto body_JSON = get_client_settings_json();

        char *body = nullptr;
        int body_len = 0;
        body_len = json::measureJson(body_JSON); /* returns 0 on fail */
        body = new char[body_len];
        if (!(*body)) {
            printf("Out Of Memory");
            body_len = 0;
        } else {
            json::serializeJson(body_JSON, body, body_len);
        }
        httpd_post_response(hs, body, body_len, "json"); //
    }

    /* this function must ALWAYS free the pbuf it is passed or it will leak memory */
    pbuf_free(unique_pbuf);

    return ret;
}
