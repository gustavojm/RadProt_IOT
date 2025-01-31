#pragma once

#include <ArduinoJson.h>
#include <sys/types.h>

typedef struct {
    uint32_t ip_address;
    uint32_t network_mask;
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
    char network_name[32];
    char network_password[32];
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
    bool is_number;
    float scale_factor;
    int average_count;
    char topic[10];

    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["enabled"] = enabled;
        json["name"] = name;
        json["start"] = start;
        json["end"] = end;
        json["is_number"] = is_number;
        json["scale_factor"] = scale_factor;
        json["average_count"] = average_count;
        json["topic"] = topic;
        return json;
    }
};

class detector_settings_entry {
  public:
    uint baudrate;
    publish_settings_entry publish_settings[10];

    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["baudrate"] = baudrate;
        auto publish_settings_array = json["publish_settings"].to<ArduinoJson::JsonArray>();

        for (auto entry : publish_settings) {
            publish_settings_array.add(entry.to_json());
        }
        return json;
    }
};

class client_settings {
  public:
    uint32_t ip_address;
    uint32_t network_mask;
    uint32_t gw;
    char wifi_name[32];
    char wifi_password[32];

    uint32_t mqtt_ip_address;
    char mqtt_password[32];

    detector_settings_entry detector_settings[3];

    ArduinoJson::JsonDocument to_json() const {
        ArduinoJson::JsonDocument json;
        json["ip_address"] = ip_address;
        printf("here1");
        json["network_mask"] = network_mask;
        json["gw"] = gw;
        json["wifi_name"] = wifi_name;
        json["wifi_password"] = wifi_password;
        json["mqtt_ip_address"] = mqtt_ip_address;
        printf("here2");
        json["mqtt_password"] = mqtt_password;

        auto detector_settings_array = json["detector_settings"].to<ArduinoJson::JsonArray>();

        for (auto entry : detector_settings) {
            printf("here3");
            detector_settings_array.add(entry.to_json());
        }
        return json;
    }
};

const client_settings *get_client_settings();

void write_client_settings(const client_settings *new_settings);

ArduinoJson::JsonDocument get_client_settings_json();
