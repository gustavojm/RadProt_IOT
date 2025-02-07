#pragma once

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
};

inline ArduinoJson::JsonDocument to_json(const publish_settings_entry *pse) {
    ArduinoJson::JsonDocument json;
    json["enabled"] = pse->enabled;
    json["name"] = pse->name;
    json["start"] = pse->start;
    json["end"] = pse->end;
    json["is_num"] = pse->is_num;
    json["scale"] = pse->scale;
    json["avg_cnt"] = pse->avg_cnt;
    json["topic"] = pse->topic;
    return json;
}

struct sensor_settings_entry {
  public:
    uint baudrate;
    publish_settings_entry publish_settings[MAX_PUBLISH_SETTINGS];
};

inline ArduinoJson::JsonDocument to_json(const sensor_settings_entry *sse) {
    ArduinoJson::JsonDocument json;
    json["baudrate"] = sse->baudrate;
    auto publish_settings_array = json["publish_settings"].to<ArduinoJson::JsonArray>();

    for (auto &entry : sse->publish_settings) {
        publish_settings_array.add(to_json(&entry));
    }
    return json;
}


struct wifi_settings {
    char ssid[32];
    char password[32];
    bool dhcp;
    ip_addr_t ip;
    ip_addr_t nm;
    ip_addr_t gw;
    ip_addr_t dns;
};

inline ArduinoJson::JsonDocument to_json(const wifi_settings *ws) {
    ArduinoJson::JsonDocument json;
    json["ssid"] = ws->ssid;
    json["password"] = ws->password;
    json["dhcp"] = ws->dhcp;
    json["ip"] = ws->ip.addr;
    json["nm"] = ws->nm.addr;
    json["gw"] = ws->gw.addr;
    json["dns"] = ws->dns.addr;
    return json;
}

struct mqtt_settings {
    char broker[32];
    uint16_t broker_port;
    char username[32];
    char password[32];    
};

inline ArduinoJson::JsonDocument to_json(const mqtt_settings *ms) {
    ArduinoJson::JsonDocument json;
    json["broker"] = ms->broker;
    json["broker_port"] = ms->broker_port;
    json["username"] = ms->username;
    json["password"] = ms->password;
    return json;
}

struct client_settings {
  public:
    wifi_settings wifi;
    mqtt_settings mqtt;
    sensor_settings_entry sensor_settings[MAX_SERIAL_SENSORS];
};

inline ArduinoJson::JsonDocument to_json(const client_settings *cs) {
    ArduinoJson::JsonDocument json;
    json["wifi"] = to_json(&cs->wifi);
    json["mqtt"] = to_json(&cs->mqtt);

    // auto sensor_settings_array = json["sensor_settings"].to<ArduinoJson::JsonArray>();
    // for (auto &entry : sensor_settings) {
    //     sensor_settings_array.add(entry.to_json());
    // }

    return json;
}


const client_settings *get_client_settings();

void write_client_settings(const client_settings *new_settings);

ArduinoJson::JsonDocument get_client_settings_json();
