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

#include "hardware/flash.h"
#include "httpd.h"
#include "httpd_structs.h"
#include <ArduinoJson.hpp>
#include <hardware/watchdog.h>
#include <pico/flash.h>
#include <settings.h>
#include <stdio.h>
#include <string.h>

#include "wifi_fns.h"

#include "post.h"
#include "firmware_update.h"
#include "serial.h"
#include "status.h"
#include "watchdog.h"

namespace json = ArduinoJson;

// ---------------------------------------------------------------------------
// Obfuscation: xorshift32 PRNG (fixed seed), XOR each byte, fixed-size pad.
// XOR is its own inverse, so the same function obfuscates and de-obfuscates.
// For JSON transport the result is hex-encoded.
// ---------------------------------------------------------------------------
static void obfuscate(char *str, size_t fixed_size) {
    if (!str || fixed_size == 0) return;
    uint32_t state = 0xDEADBEEFu;
    for (size_t i = 0; i < fixed_size; i++) {
        if (i % 4 == 0) {
            uint32_t x = state;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            state = x;
        }
        str[i] ^= (state >> ((i % 4) * 8)) & 0xFF;
    }
}

static void hex_encode(const uint8_t *in, size_t in_len, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < in_len; i++) {
        out[i * 2]     = hex[in[i] >> 4];
        out[i * 2 + 1] = hex[in[i] & 0xF];
    }
    out[in_len * 2] = '\0';
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void hex_decode(const char *in, uint8_t *out, size_t out_len) {
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_val(in[i * 2]);
        int lo = hex_val(in[i * 2 + 1]);
        out[i] = (uint8_t)((hi << 4) | (lo & 0xF));
    }
}

/* POST handlers functions */

json::MyJsonDocument settings_get_fn(struct http_state *hs) {
    auto responseJson = get_client_mode_settings_json();

    responseJson["wifi"]["auth_mode"] = connect_auth_mode_to_scan_auth_mode(responseJson["wifi"]["auth_mode"]);

    responseJson["initial_config"] = initial_config;
    responseJson["firmware_version"] = FIRMWARE_VERSION;
    return responseJson;
}

__attribute__((optimize("O0")))
static bool verify_settings_password(const json::MyJsonDocument &post_data, json::MyJsonDocument &responseJson,
                                     char *config_pwd, size_t config_pwd_size) {

                                        char *password = (char *)post_data["auth_password"].as<const char*>();
                                        char *settings_password = (char *)get_client_mode_settings()->password;

    if (initial_config) {
        if (strcmp((char *)post_data["auth_password"].as<const char*>(), "") == 0 && strcmp((char *)get_client_mode_settings()->password, "") == 0) {
            responseJson["message"] = "Define a Password to Protect Settings";
            return false;
        }
    } else if (strcmp((char *)post_data["auth_password"].as<const char*>(), (char *)get_client_mode_settings()->password) != 0) {
        responseJson["message"] = "Wrong Password";
        return false;
    }
    
    config_pwd[0] = '\0';    
    if (post_data["mangled"].as<int>() == 1) {
        const char *hex = post_data["settings"]["password"].as<const char*>();
        if (hex) {
            hex_decode(hex, (uint8_t *)config_pwd, config_pwd_size);
            obfuscate(config_pwd, config_pwd_size);
        }
    } else {
        const char *raw = post_data["settings"]["password"].as<const char*>();
        if (raw) {
            strncpy(config_pwd, raw, config_pwd_size);
            config_pwd[config_pwd_size - 1] = '\0';
        }
    }
    return true;
}

bool apply_settings_from_json(json::MyJsonDocument &post_data, json::MyJsonDocument &responseJson, bool skip_password_check) {
    client_mode_settings_union new_settings;
    new_settings.settings = *get_client_mode_settings();

    char config_pwd[sizeof new_settings.settings.password];
    if (!verify_settings_password(post_data, responseJson, config_pwd, sizeof config_pwd)) {
        return false;
    }

    if (config_pwd[0] != '\0') {
        strncpy(
            (char *)new_settings.settings.password,
            config_pwd,
            sizeof new_settings.settings.password);
    }

    strncpy(new_settings.settings.wifi.ssid, post_data["wifi"]["ssid"], sizeof new_settings.settings.wifi.ssid);
    new_settings.settings.wifi.auth_mode = scan_auth_mode_to_connect_auth_mode(atoi(post_data["wifi"]["auth_mode"]));

    new_settings.settings.wifi.ipv4.dhcp = post_data["wifi"]["dhcp"];

    ipaddr_aton(post_data["wifi"]["ip"], &new_settings.settings.wifi.ipv4.ip);
    ipaddr_aton(post_data["wifi"]["nm"], &new_settings.settings.wifi.ipv4.nm);
    ipaddr_aton(post_data["wifi"]["gw"], &new_settings.settings.wifi.ipv4.gw);

    new_settings.settings.eth.ipv4.dhcp = post_data["eth"]["dhcp"];

    ipaddr_aton(post_data["eth"]["ip"], &new_settings.settings.eth.ipv4.ip);
    ipaddr_aton(post_data["eth"]["nm"], &new_settings.settings.eth.ipv4.nm);
    ipaddr_aton(post_data["eth"]["gw"], &new_settings.settings.eth.ipv4.gw);

    new_settings.settings.conn_type = post_data["conn_type"] == "WIFI" ? WIFI : ETHERNET;

    strncpy(new_settings.settings.mqtt.broker, post_data["mqtt"]["broker"], sizeof new_settings.settings.mqtt.broker);
    new_settings.settings.mqtt.port = atoi(post_data["mqtt"]["port"]);
    strncpy(
        new_settings.settings.mqtt.username, post_data["mqtt"]["username"], sizeof new_settings.settings.mqtt.username);

    if (post_data["mangled"].as<int>() == 1) {
        const char *hex;
        hex = post_data["wifi"]["password"].as<const char*>();
        if (hex) {
            hex_decode(hex, (uint8_t *)new_settings.settings.wifi.password,
                       sizeof new_settings.settings.wifi.password);
            obfuscate(new_settings.settings.wifi.password, sizeof new_settings.settings.wifi.password);
        }
        hex = post_data["mqtt"]["password"].as<const char*>();
        if (hex) {
            hex_decode(hex, (uint8_t *)new_settings.settings.mqtt.password,
                       sizeof new_settings.settings.mqtt.password);
            obfuscate(new_settings.settings.mqtt.password, sizeof new_settings.settings.mqtt.password);
        }
    } else {
        strncpy(new_settings.settings.wifi.password, post_data["wifi"]["password"],
                sizeof new_settings.settings.wifi.password);
        strncpy(new_settings.settings.mqtt.password, post_data["mqtt"]["password"],
                sizeof new_settings.settings.mqtt.password);
    }

    for (int i = 0; i < MAX_SERIAL_SENSORS; i++) {
        auto s_s = post_data["s_s"][i];
        new_settings.settings.sensor_settings[i].baudrate = atoi(s_s["baud"]);
        new_settings.settings.sensor_settings[i].enabled = s_s["enabled"];
        new_settings.settings.sensor_settings[i].simulate_values = s_s["simulate_values"];

        for (int j = 0; j < MAX_PUBLISH_SETTINGS; j++) {
            auto p_s = s_s["p_s"][j];
            new_settings.settings.sensor_settings[i].publish_settings[j].enabled = p_s["enabled"];

            strncpy(
                new_settings.settings.sensor_settings[i].publish_settings[j].name,
                p_s["name"],
                sizeof new_settings.settings.sensor_settings[i].publish_settings[j].name);
            new_settings.settings.sensor_settings[i].publish_settings[j].start = p_s["start"];
            new_settings.settings.sensor_settings[i].publish_settings[j].end = p_s["end"];
            new_settings.settings.sensor_settings[i].publish_settings[j].is_num = p_s["is_num"];
            new_settings.settings.sensor_settings[i].publish_settings[j].avg_cnt = atoi(p_s["avg_cnt"]);
            new_settings.settings.sensor_settings[i].publish_settings[j].scale = atof(p_s["scale"]);
            strncpy(
                new_settings.settings.sensor_settings[i].publish_settings[j].topic,
                p_s["topic"],
                sizeof new_settings.settings.sensor_settings[i].publish_settings[j].topic);
        }
    }

    flash_safe_execute(write_client_mode_settings, &new_settings, UINT32_MAX);
    return true;
}

json::MyJsonDocument settings_save_fn(struct http_state *hs) {
    auto responseJson = json::MyJsonDocument();
    auto post_data = json::MyJsonDocument();
    json::DeserializationError error = json::deserializeJson(post_data, hs->post_content, hs->post_content_len);

    if (error) {
        lDebug(Error, "Error json parse. %s", error.c_str());
        printf("%.*s", hs->post_content_len, hs->post_content);
    } else {
        client_mode_settings old_settings = *get_client_mode_settings();

        if (apply_settings_from_json(post_data, responseJson)) {
            bool network_settings_changed =
                (old_settings.conn_type != get_client_mode_settings()->conn_type) ||
                (memcmp(&old_settings.wifi, &get_client_mode_settings()->wifi, sizeof(wifi_settings)) != 0) ||
                (memcmp(&old_settings.eth, &get_client_mode_settings()->eth, sizeof(ethernet_settings)) != 0);

            bool mqtt_settings_changed =
                memcmp(&old_settings.mqtt, &get_client_mode_settings()->mqtt, sizeof(mqtt_settings)) != 0;

            bool sensors_settings_changed = false;
            for (int i = 0; i < MAX_SERIAL_SENSORS; i++) {
                sensors_settings_changed |=
                    old_settings.sensor_settings[i].communication_settings_changed(get_client_mode_settings()->sensor_settings[i]);
            }

            if (network_settings_changed || sensors_settings_changed || mqtt_settings_changed) {
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

json::MyJsonDocument restart_fn(struct http_state *hs) {
    auto responseJson = json::MyJsonDocument();

    vTaskSuspend(feedWdTask_handle);
    watchdog_reboot(0, SRAM_END, 1000);

    responseJson.to<json::JsonObject>(); // Because we have not created any keys, it is just empty
    return responseJson;
}

json::MyJsonDocument firmware_upload_fn(struct http_state *hs) {
    LWIP_UNUSED_ARG(hs);
    return firmware_upload_status_json();
}

json::MyJsonDocument get_settings_backup_json() {
    auto json = json::MyJsonDocument();
    const client_mode_settings *settings = get_client_mode_settings();

    json["mangled"] = 1;
    {
        char mac_str[18];
        snprintf(
            mac_str,
            sizeof mac_str,
            "%02X-%02X-%02X-%02X-%02X-%02X",
            wifi_mac[0], wifi_mac[1], wifi_mac[2], wifi_mac[3], wifi_mac[4], wifi_mac[5]);
        json["mac"] = mac_str;
    }
    json["conn_type"] = settings->conn_type == WIFI ? "WIFI" : "ETHERNET";

    json["wifi"]["ssid"] = settings->wifi.ssid;
    {
        char buf[sizeof settings->wifi.password];
        strncpy(buf, settings->wifi.password, sizeof buf);
        obfuscate(buf, sizeof buf);
        char hex[sizeof buf * 2 + 1];
        hex_encode((uint8_t *)buf, sizeof buf, hex);
        json["wifi"]["password"] = hex;
    }
    {
        char buf[8];
        snprintf(buf, sizeof buf, "%d", connect_auth_mode_to_scan_auth_mode(settings->wifi.auth_mode));
        json["wifi"]["auth_mode"] = buf;
    }
    json["wifi"]["dhcp"] = settings->wifi.ipv4.dhcp;
    json["wifi"]["ip"] = ipaddr_ntoa(&settings->wifi.ipv4.ip);
    json["wifi"]["nm"] = ipaddr_ntoa(&settings->wifi.ipv4.nm);
    json["wifi"]["gw"] = ipaddr_ntoa(&settings->wifi.ipv4.gw);

    json["eth"]["dhcp"] = settings->eth.ipv4.dhcp;
    json["eth"]["ip"] = ipaddr_ntoa(&settings->eth.ipv4.ip);
    json["eth"]["nm"] = ipaddr_ntoa(&settings->eth.ipv4.nm);
    json["eth"]["gw"] = ipaddr_ntoa(&settings->eth.ipv4.gw);

    json["mqtt"]["broker"] = settings->mqtt.broker;
    {
        char buf[8];
        snprintf(buf, sizeof buf, "%u", settings->mqtt.port);
        json["mqtt"]["port"] = buf;
    }
    json["mqtt"]["username"] = settings->mqtt.username;
    {
        char buf[sizeof settings->mqtt.password];
        strncpy(buf, settings->mqtt.password, sizeof buf);
        obfuscate(buf, sizeof buf);
        char hex[sizeof buf * 2 + 1];
        hex_encode((uint8_t *)buf, sizeof buf, hex);
        json["mqtt"]["password"] = hex;
    }

    for (int i = 0; i < MAX_SERIAL_SENSORS; i++) {
        auto &s = settings->sensor_settings[i];
        char baud_buf[16];
        snprintf(baud_buf, sizeof baud_buf, "%u", s.baudrate);
        json["s_s"][i]["baud"] = baud_buf;
        json["s_s"][i]["enabled"] = s.enabled;
        json["s_s"][i]["simulate_values"] = s.simulate_values;

        for (int j = 0; j < MAX_PUBLISH_SETTINGS; j++) {
            auto &p = s.publish_settings[j];
            char avg_buf[16], scale_buf[16];
            snprintf(avg_buf, sizeof avg_buf, "%u", p.avg_cnt);
            snprintf(scale_buf, sizeof scale_buf, "%.2f", (double)p.scale);
            json["s_s"][i]["p_s"][j]["enabled"] = p.enabled;
            json["s_s"][i]["p_s"][j]["name"] = p.name;
            json["s_s"][i]["p_s"][j]["start"] = p.start;
            json["s_s"][i]["p_s"][j]["end"] = p.end;
            json["s_s"][i]["p_s"][j]["is_num"] = p.is_num;
            json["s_s"][i]["p_s"][j]["avg_cnt"] = avg_buf;
            json["s_s"][i]["p_s"][j]["scale"] = scale_buf;
            json["s_s"][i]["p_s"][j]["topic"] = p.topic;
        }
    }

    {
        char buf[sizeof settings->password];
        strncpy(buf, (char *)settings->password, sizeof buf);
        obfuscate(buf, sizeof buf);
        char hex[sizeof buf * 2 + 1];
        hex_encode((uint8_t *)buf, sizeof buf, hex);
        json["settings"]["password"] = hex;
    }

    return json;
}

json::MyJsonDocument settings_backup_fn(struct http_state *hs) {
    auto responseJson = json::MyJsonDocument();
    auto post_data = json::MyJsonDocument();
    json::DeserializationError error = json::deserializeJson(post_data, hs->post_content, hs->post_content_len);

    if (error) {
        responseJson["message"] = "Invalid JSON";
        return responseJson;
    }

    char config_pwd[sizeof(((client_mode_settings *)nullptr)->password)];
    if (!verify_settings_password(post_data, responseJson, config_pwd, sizeof config_pwd)) {
        return responseJson;
    }

    return get_settings_backup_json();
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
    {
        "/restart.cgi",
        &restart_fn,
    },
    {
        "/firmware_upload.cgi",
        &firmware_upload_fn,
    },
    {
        "/settings_backup.cgi",
        &settings_backup_fn,
    },

};

err_t httpd_process_post_data(struct http_state *hs) {
    if (hs->post_uri) {
        for (auto &entry : post_handlers) {
            if (strncmp(hs->post_uri, entry.handler_name, strlen(entry.handler_name)) == 0) {
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
                return ERR_OK;
            }
        }
    }
    return ERR_OK;
}
