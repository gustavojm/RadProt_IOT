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
#include <hardware/watchdog.h>
#include <pico/flash.h>
#include <settings.h>
#include <stdio.h>
#include <string.h>
#include "hardware/flash.h"

#include "wifi_fns.h"

#include "post.h"
#include "status.h"
#include "watchdog.h"

namespace json = ArduinoJson;


/* POST handlers functions */

json::MyJsonDocument settings_get_fn(struct http_state *hs) {
    auto responseJson = get_client_mode_settings_json();

    responseJson["wifi"]["auth_mode"] = connect_auth_mode_to_scan_auth_mode(responseJson["wifi"]["auth_mode"]);

    responseJson["initial_config"] = initial_config;
    return responseJson;
}

json::MyJsonDocument settings_save_fn(struct http_state *hs) {
    auto responseJson = json::MyJsonDocument();
    auto post_data = json::MyJsonDocument();
    json::DeserializationError error = json::deserializeJson(post_data, hs->post_content, hs->post_content_len);

    if (error) {
        lDebug(Error, "Error json parse. %s", error.c_str());
    } else {
        static client_mode_settings_t cs;
        client_mode_settings old_settings;
        cs.settings = *get_client_mode_settings();

        old_settings = cs.settings;

        strncpy(cs.settings.wifi.ssid, post_data["wifi"]["ssid"], sizeof cs.settings.wifi.ssid);
        strncpy(cs.settings.wifi.password, post_data["wifi"]["password"], sizeof cs.settings.wifi.password);
        cs.settings.wifi.auth_mode = scan_auth_mode_to_connect_auth_mode(atoi(post_data["wifi"]["auth_mode"]));

        cs.settings.wifi.ipv4.dhcp = post_data["wifi"]["dhcp"];

        ipaddr_aton(post_data["wifi"]["ip"], &cs.settings.wifi.ipv4.ip);
        ipaddr_aton(post_data["wifi"]["nm"], &cs.settings.wifi.ipv4.nm);
        ipaddr_aton(post_data["wifi"]["gw"], &cs.settings.wifi.ipv4.gw);

        cs.settings.eth.ipv4.dhcp = post_data["eth"]["dhcp"];

        ipaddr_aton(post_data["eth"]["ip"], &cs.settings.eth.ipv4.ip);
        ipaddr_aton(post_data["eth"]["nm"], &cs.settings.eth.ipv4.nm);
        ipaddr_aton(post_data["eth"]["gw"], &cs.settings.eth.ipv4.gw);

        cs.settings.conn_type = post_data["conn_type"] == "WIFI" ? WIFI : ETHERNET;
        
        strncpy(cs.settings.mqtt.broker, post_data["mqtt"]["broker"], sizeof cs.settings.mqtt.broker);
        cs.settings.mqtt.port = atoi(post_data["mqtt"]["port"]);
        strncpy(cs.settings.mqtt.username, post_data["mqtt"]["username"], sizeof cs.settings.mqtt.username);
        strncpy(cs.settings.mqtt.password, post_data["mqtt"]["password"], sizeof cs.settings.mqtt.password);

        int elems = post_data["s_s"].size();

        for (int i = 0; i < MAX_SERIAL_SENSORS; i++) {
            auto s_s = post_data["s_s"][i];        
            cs.settings.sensor_settings[i].baudrate = atoi(s_s["baud"]);
            cs.settings.sensor_settings[i].enabled = s_s["enabled"];

            for (int j = 0; j < MAX_PUBLISH_SETTINGS; j++) {
                auto p_s = s_s["p_s"][j];
                cs.settings.sensor_settings[i].publish_settings[j].enabled = p_s["enabled"];

                strncpy(
                    cs.settings.sensor_settings[i].publish_settings[j].name,
                    p_s["name"],
                    sizeof cs.settings.sensor_settings->publish_settings->name);
                cs.settings.sensor_settings[i].publish_settings[j].start = p_s["start"];
                cs.settings.sensor_settings[i].publish_settings[j].end = p_s["end"];
                cs.settings.sensor_settings[i].publish_settings[j].is_num = p_s["is_num"];
                cs.settings.sensor_settings[i].publish_settings[j].avg_cnt = atoi(p_s["avg_cnt"]);
                cs.settings.sensor_settings[i].publish_settings[j].scale = atof(p_s["scale"]);
                strncpy(
                    cs.settings.sensor_settings[i].publish_settings[j].topic,
                    p_s["topic"],
                    sizeof cs.settings.sensor_settings->publish_settings->topic);
            }
        }

        bool save_settings = true;

        const client_mode_settings *current_settings = get_client_mode_settings();

        if (initial_config) {
            if (strcmp(post_data["settings"]["password"], "") == 0 && strcmp((char *)current_settings->password, "") == 0) {
                responseJson["message"] = "Define a Password to Protect Settings";
                save_settings = false;
            };

            if (post_data["settings"]["password"]) {
                strncpy((char *)cs.settings.password, post_data["settings"]["password"], sizeof cs.settings.password);
            }
        } else {
            if (post_data["settings"]["password"] != current_settings->password) {
                responseJson["message"] = "Wrong Password";
                save_settings = false;
            } 
        }

        if (save_settings) {
            flash_safe_execute(write_client_mode_settings, &cs, UINT32_MAX);

            bool network_settings_changed = (old_settings.conn_type != cs.settings.conn_type) ||
                (memcmp(&old_settings.wifi, &cs.settings.wifi, sizeof(wifi_settings)) != 0) ||
                (memcmp(&old_settings.eth, &cs.settings.eth, sizeof(ethernet_settings)) != 0);

            bool mqtt_settings_changed = memcmp(&old_settings.mqtt, &cs.settings.mqtt, sizeof(mqtt_settings)) != 0;

            if (network_settings_changed || (mqtt_settings_changed && initial_config)) {
                    responseJson["OK"] = "Rebooting";
                    watchdog_reboot(0, SRAM_END, 1000);
                    vTaskSuspend(feedWdTask_handle);
            } else {    
                if (mqtt_settings_changed) {
                    mqtt_reconnect = true;
                }
                responseJson["populate_readings"] = true;
                responseJson["message"] = "Settings saved";
            }
        }        
    }
    return responseJson;
}

json::MyJsonDocument wifi_nets_fn(struct http_state *hs) {    
    auto responseJson = json::MyJsonDocument();
    auto wifi_nets_array = responseJson.to<json::JsonArray>();

    for (auto &wifi_net : wifi_networks) {
        auto wifi_net_entry = json::MyJsonDocument();
        wifi_net_entry["ssid"] = wifi_net.ssid;
        wifi_net_entry["rssi"] = wifi_net.rssi;
        wifi_net_entry["chann"] = wifi_net.channel;
        wifi_net_entry["auth_mode"] = wifi_net.auth_mode;

        char bssid[18];
        snprintf(
            bssid,
            sizeof bssid,
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
    return responseJson;
}

json::MyJsonDocument wifi_nets_scan_fn(struct http_state *hs) {
    wifi_networks_scan(false);

    return wifi_nets_fn(hs);
}
    
// @formatter:off
const post_handler_entry post_handlers[] = {
    {
        "/settings_get.cgi", /* Handler name */
        &settings_get_fn,    /* Associated function */
    },
    {
        "/settings_save.cgi",
        &settings_save_fn,
    },
    {
        "/wifi_nets.cgi",
        &wifi_nets_fn,
    },
    {
        "/wifi_nets_scan.cgi",
        &wifi_nets_scan_fn,
    },

};

err_t httpd_process_post_data(struct http_state *hs) {
    if (hs->post_uri) {
        for (auto &entry : post_handlers) {
            if (strncmp (hs->post_uri, entry.handler_name, strlen(entry.handler_name)) == 0) {
                auto responseJson = entry.handler_function(hs);

                char *response = nullptr;
                int response_len = 0;
                response_len = json::measureJson(responseJson); /* returns 0 on fail */
                response = new char[response_len];
                if (!(response)) {
                    lDebug(Error, "Out Of Memory");
                    response_len = 0;
                } else {
                    json::serializeJson(responseJson, response, response_len);
                }
                lDebug(Debug, "POST DATA: %*.s \n", response_len, response);
                httpd_post_response(hs, response, response_len, "json");
                delete[] response;
                return ERR_OK;
            }
        }
    }
    return ERR_OK;
}
