#pragma once

#include <ArduinoJson.h>
#include <sys/types.h>

typedef struct {
    uint32_t ip;
    uint32_t net_mask;
    /* The secondary IP address is needed to support the "sign into network" mechanism.
     * Modern OSes will automatically show the 'sign into network' page if:
     *	1. The network has valid DHCP/DNS servers
     *	2. The DNS server resolves requests to test names to valid EXTERNAL IPs (not 192.168.x.y)
     *	3. Issuing a HTTP GET request to the external IP results in a HTTP 302 redirect to the login page.
     *
     * E.g. see
     *https://cs.android.com/android/platform/superproject/+/master:packages/modules/NetworkStack/src/com/android/server/connectivity/NetworkMonitor.java,
     *		specifically the isDnsPrivateIpResponse() check and the "DNS response to the URL is private IP" error.
     */
    uint32_t secondary_address;
    char ssid[32];
    char password[32];
    char hostname[32];
    char domain_name[32];
    uint32_t dns_ignores_network_suffix;
} config_server_settings;

const config_server_settings *get_config_server_settings();
void write_config_server_settings(const config_server_settings *new_settings);

const char *get_next_domain_name_component(const char *domain_name, int *position, int *length);

class publish_settings_entry{
public:
    bool enabled;
    char name[10];
    uint16_t start;
    uint16_t end;
    bool is_num;
    float scale;
    int avg_cnt;
    char topic[10];

    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["enabled"] = enabled;
        json["name"] = name;
        json["start"] = start;
        json["end"] = end;
        json["is_num"] = is_num;
        json["scale"] = scale;
        json["avg_cnt"] = avg_cnt;
        json["topic"] = topic;
        return json;
    }
};

class sensor_settings_entry {
  public:
    uint baudrate;
    publish_settings_entry publish_settings[10];

    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["baudrate"] = baudrate;
        auto publish_settings_array = json["publish_settings"].to<ArduinoJson::JsonArray>();

        for (auto &entry : publish_settings) {
            publish_settings_array.add(entry.to_json());
        }
        return json;
    }
};

class wifi_settings {
public:
    char ssid[32];
    char password[32];
    bool dhcp;
    uint32_t ip;
    uint32_t net_mask;
    uint32_t gw;
    
    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["ssid"] = ssid;
        json["password"] = password;
        json["dhcp"] = dhcp;
        json["ip"] = ip;
        json["net_mask"] = net_mask;
        json["gw"] = gw;
        return json;
    }
};

class mqtt_settings {
public:
    char broker_address[32];
    uint16_t broker_port;
    char username[32];
    char password[32];
    
    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["broker_address"] = broker_address;
        json["broker_port"] = broker_port;
        json["username"] = username;
        json["password"] = password;
        return json;
    }
};


class client_settings {
  public:
    wifi_settings wifi;
    mqtt_settings mqtt;
    sensor_settings_entry sensor_settings[3];

    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["wifi"] = wifi.to_json();
        json["mqtt"] = mqtt.to_json();

        auto sensor_settings_array = json["sensor_settings"].to<ArduinoJson::JsonArray>();
        for (auto &entry : sensor_settings) {
            sensor_settings_array.add(entry.to_json());
        }

        return json;
    }
};

const client_settings *get_client_settings();

void write_client_settings(const client_settings *new_settings);

ArduinoJson::JsonDocument get_client_settings_json();
