#pragma once

#include "hardware/flash.h"
#include <ArduinoJson.h>
#include <sys/types.h>
#include <lwip/ip_addr.h>

#define MAX_SERIAL_SENSORS 3
#define MAX_PUBLISH_SETTINGS 10
#define SERIAL_BUFFERS_SIZE 256

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

struct publish_settings_entry{
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


struct sensor_settings_entry {
  public:
    uint baudrate;
    bool enabled;
    publish_settings_entry publish_settings[MAX_PUBLISH_SETTINGS];

    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["baudrate"] = baudrate;
        json["enabled"] = enabled;
        auto publish_settings_array = json["publish_settings"].to<ArduinoJson::JsonArray>();
    
        for (auto &entry : publish_settings) {
            publish_settings_array.add(entry.to_json());
        }
        return json;
    }
    
};

struct wifi_settings {
    char ssid[32];
    char password[32];
    int auth_mode;
    bool dhcp;
    ip_addr_t ip;
    ip_addr_t nm;
    ip_addr_t gw;
    ip_addr_t dns;

    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["ssid"] = ssid;
        json["password"] = password;
        json["auth_mode"] = auth_mode;
        json["dhcp"] = dhcp;
        json["ip"] = ip.addr;
        json["nm"] = nm.addr;
        json["gw"] = gw.addr;
        json["dns"] = dns.addr;
        return json;
    }
    
};

struct mqtt_settings {
    char broker[32];
    uint16_t port;
    char username[32];
    char password[32];    

    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["broker"] = broker;
        json["port"] = port;
        json["username"] = username;
        json["password"] = password;
        return json;
    }
    
};

struct client_settings {
  public:
    wifi_settings wifi;
    mqtt_settings mqtt;
    sensor_settings_entry sensor_settings[MAX_SERIAL_SENSORS];

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

union client_settings_t {
    client_settings settings;
    char padding[FLASH_SECTOR_SIZE];
} __attribute__((aligned(FLASH_SECTOR_SIZE)));

static_assert(sizeof(client_settings_t) == FLASH_SECTOR_SIZE, "Size mismatch!");

const client_settings *get_client_settings();

void __not_in_flash_func(write_client_settings)(void *param);

ArduinoJson::JsonDocument get_client_settings_json();
