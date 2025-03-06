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

namespace json = ArduinoJson;

int scan_auth_mode_to_connect_auth_mode(int scan_auth_mode) {
    uint32_t connect_auth_mode;

    switch (scan_auth_mode) {
    case 0: connect_auth_mode = CYW43_AUTH_OPEN; break;
    case 1: connect_auth_mode = CYW43_AUTH_WPA_TKIP_PSK; break;
    case 2: connect_auth_mode = CYW43_AUTH_WPA2_AES_PSK; break;
    case 3: connect_auth_mode = CYW43_AUTH_WPA2_MIXED_PSK; break;
    case 4: connect_auth_mode = CYW43_AUTH_WPA3_SAE_AES_PSK; break;
    case 5: connect_auth_mode = CYW43_AUTH_WPA3_WPA2_AES_PSK; break;
    default:
        // Handle unknown auth type
        connect_auth_mode = -1;
    }
    return connect_auth_mode;
}

int connect_auth_mode_to_scan_auth_mode(int connect_auth_mode) {
    uint32_t scan_auth_mode;

    switch (connect_auth_mode) {
    case CYW43_AUTH_OPEN: scan_auth_mode = 0; break;
    case CYW43_AUTH_WPA_TKIP_PSK: scan_auth_mode = 1 ; break;
    case CYW43_AUTH_WPA2_AES_PSK: scan_auth_mode = 2; break;
    case CYW43_AUTH_WPA2_MIXED_PSK: scan_auth_mode = 3; break;
    case CYW43_AUTH_WPA3_SAE_AES_PSK: scan_auth_mode = 4; break;
    case CYW43_AUTH_WPA3_WPA2_AES_PSK: scan_auth_mode = 5; break;
    default:
        // Handle unknown auth type
        scan_auth_mode = -1;
    }
    return scan_auth_mode;
}

err_t httpd_process_post_data(struct http_state *hs) {    
    auto responseJson = json::MyJsonDocument();
    if (hs->post_uri && !memcmp(hs->post_uri, "/settings_save.cgi", 19)) {        
        auto post_data = json::MyJsonDocument();
        json::DeserializationError error = json::deserializeJson(post_data, hs->post_content, hs->post_content_len);

        if (error) {
            printf("Error json parse. %s", error.c_str());
        } else {
            static client_settings_t cs;
            cs.settings = *get_client_settings();

            strncpy(cs.settings.wifi.ssid, post_data["wifi"]["ssid"], sizeof cs.settings.wifi.ssid);
            strncpy(cs.settings.wifi.password, post_data["wifi"]["password"], sizeof cs.settings.wifi.password);
            cs.settings.wifi.auth_mode = scan_auth_mode_to_connect_auth_mode(atoi(post_data["wifi"]["auth_mode"]));
            
            cs.settings.wifi.dhcp = post_data["wifi"]["dhcp"];

            ipaddr_aton(post_data["wifi"]["ip"], &cs.settings.wifi.ip);
            ipaddr_aton(post_data["wifi"]["nm"], &cs.settings.wifi.nm);
            ipaddr_aton(post_data["wifi"]["gw"], &cs.settings.wifi.gw);
        
            strncpy(cs.settings.mqtt.broker, post_data["mqtt"]["broker"], sizeof cs.settings.mqtt.broker);
            cs.settings.mqtt.port = atoi(post_data["mqtt"]["port"]);
            strncpy(cs.settings.mqtt.username, post_data["mqtt"]["username"], sizeof cs.settings.mqtt.username);
            strncpy(cs.settings.mqtt.password, post_data["mqtt"]["password"], sizeof cs.settings.mqtt.password);

            int elems = post_data["s_s"].size();
            printf("elements : %i \n", elems);

            for (int i = 0; i < MAX_SERIAL_SENSORS; i++) {
                auto s_s = post_data["s_s"][i];
                printf("BAUD RATE: %s", s_s["baud"]);
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

            const client_settings *current_settings = get_client_settings();

            if (initial_config) {
                strncpy(
                        (char *)cs.settings.password,
                        post_data["settings"]["password"],
                        sizeof cs.settings.password);

            } else {
                if (post_data["settings"]["password"] != current_settings->password) {
                    responseJson["error"] = "Wrong Password";
                    save_settings = false;
                } else {
                    responseJson["OK"] = "Rebooting";
                }
            }

            char *response = nullptr;
            int response_len = 0;
            response_len = json::measureJson(responseJson); /* returns 0 on fail */
            response = new char[response_len];
            if (!(response)) {
                printf("Out Of Memory");
                response_len = 0;
            } else {
                json::serializeJson(responseJson, response, response_len);
            }
            httpd_post_response(hs, response, response_len, "json");

            if (!save_settings) {
                return ERR_OK;
            }

            flash_safe_execute(write_client_settings, &cs, UINT32_MAX);
            watchdog_reboot(0, SRAM_END, 500);
        }
    }

    // if (hs->post_uri && !memcmp(hs->post_uri, "/settings.cgi", 14)) {
    //     auto post_data = json::MyJsonDocument();
    //     json::DeserializationError error = json::deserializeJson(post_data, hs->post_content, hs->post_content_len);

    //     if (error) {
    //         printf("Error json parse. %s", error.c_str());
    //     } else {
    //         /* Extracting data from form */
    //         char const *ssid = post_data["ssid"];
    //     }
    //     ret = ERR_OK;

    //     auto body_JSON = json::MyJsonDocument();

    //     char *body = nullptr;
    //     int body_len = 0;
    //     body_len = json::measureJson(body_JSON); /* returns 0 on fail */
    //     body = new char[body_len];
    //     if (!(body)) {
    //         printf("Out Of Memory");
    //         body_len = 0;
    //     } else {
    //         json::serializeJson(body_JSON, body, body_len);
    //     }
    //     httpd_post_response(hs, body, body_len, "json"); // indicate JSON IMPROVE THIS
    // }

    if (hs->post_uri && !memcmp(hs->post_uri, "/wifi_nets.cgi", 15)) {
        auto body_JSON = json::MyJsonDocument();
        auto wifi_nets_array = body_JSON.to<json::JsonArray>();

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

        char *body = nullptr;
        int body_len = 0;
        body_len = json::measureJson(body_JSON); /* returns 0 on fail */
        body = new char[body_len];
        if (!(body)) {
            printf("Out Of Memory");
            body_len = 0;
        } else {
            json::serializeJson(body_JSON, body, body_len);
        }
        httpd_post_response(hs, body, body_len, "json"); // indicate JSON IMPROVE THIS
    }

    if (hs->post_uri && !memcmp(hs->post_uri, "/settings_get.cgi", 18)) {
        auto body_JSON = get_client_settings_json();

        body_JSON["wifi"]["auth_mode"] = connect_auth_mode_to_scan_auth_mode(body_JSON["wifi"]["auth_mode"]);

        uint8_t itf_sta_mac[6];
        cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, itf_sta_mac);
        char mac_addr_str[18];
        snprintf(mac_addr_str, sizeof mac_addr_str, "%02x:%02x:%02x:%02x:%02x:%02x\n",
        itf_sta_mac[0], itf_sta_mac[1], itf_sta_mac[2], itf_sta_mac[3], itf_sta_mac[4], itf_sta_mac[5]);    
        body_JSON["mac_address"] =  mac_addr_str;
        body_JSON["initial_config"] = initial_config;

        char *body = nullptr;
        int body_len = 0;
        body_len = json::measureJson(body_JSON); /* returns 0 on fail */
        // body_len++;         // place for null terminator
        body = new char[body_len];
        if (!body) {
            printf("Out Of Memory");
            body_len = 0;
        } else {
            json::serializeJson(body_JSON, body, body_len);
            // body[body_len] = '\0';
        }

        httpd_post_response(hs, body, body_len, "json"); //
    }

    return ERR_OK;
}