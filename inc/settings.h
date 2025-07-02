#pragma once

#include "hardware/flash.h"
#include <arduinojson_cust_alloc.h>
#include <sys/types.h>
#include <lwip/ip_addr.h>
#include <cstring>

#include "etl/string.h"
#include "etl/set.h"
#include "etl/iterator.h"

#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include "enc28j60_LWIP_FreeRTOS.h"

inline const uint INITIAL_CONFIG_GPIO = 14;     // Pin 19
inline const uint STATUS_LED_GPIO = 15;         // Pin 20

inline volatile bool initial_config = false;

// Define the map with string as key and array as value
// Define comparison operator for cyw43_ev_scan_result_t
inline bool operator<(const cyw43_ev_scan_result_t& lhs, const cyw43_ev_scan_result_t& rhs) {
    // compare by BSSID
    return std::memcmp(lhs.bssid, rhs.bssid, sizeof(lhs.bssid)) < 0;
}

constexpr size_t MAX_WIFI_NETWORKS = 10;

inline etl::set<cyw43_ev_scan_result_t, MAX_WIFI_NETWORKS> wifi_networks;

#define MAX_SERIAL_SENSORS 3
#define MAX_PUBLISH_SETTINGS 10
#define SERIAL_BUFFERS_SIZE 256

typedef struct {
    uint32_t ip;
    uint32_t nm;
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
    char ssid[28];
    char password[32];
    char hostname[32];
    char domain_name[32];
    uint32_t dns_ignores_network_suffix;
} ap_mode_settings;

const ap_mode_settings *get_ap_mode_settings();
void write_ap_mode_settings(const ap_mode_settings *new_settings);

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

    ArduinoJson::MyJsonDocument to_json() const {
        ArduinoJson::MyJsonDocument json;
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

    ArduinoJson::MyJsonDocument to_json() const {
        ArduinoJson::MyJsonDocument json;
        json["baudrate"] = baudrate;
        json["enabled"] = enabled;
        auto publish_settings_array = json["publish_settings"].to<ArduinoJson::JsonArray>();
    
        for (auto &entry : publish_settings) {
            publish_settings_array.add(entry.to_json());
        }
        return json;
    }
    
};

struct ipv4_settings {
    bool dhcp;
    ip_addr_t ip;
    ip_addr_t nm;
    ip_addr_t gw;
    ip_addr_t dns;  
};

struct ethernet_settings { 
    ipv4_settings ipv4; 

    ArduinoJson::MyJsonDocument to_json() const {
        ArduinoJson::MyJsonDocument json;
        json["is_available"] = enc28j60_state.is_available;
        if (enc28j60_state.is_available) {
            json["ip"] = ipv4.ip.addr;
            json["nm"] = ipv4.nm.addr;
            json["gw"] = ipv4.gw.addr;
            json["dns"] = ipv4.dns.addr;
            
            char mac_addr_str[18];
            snprintf(
                mac_addr_str,
                sizeof mac_addr_str,
                "%02X:%02X:%02X:%02X:%02X:%02X\n",
                enc28j60_state.mac_[0],
                enc28j60_state.mac_[1],
                enc28j60_state.mac_[2],
                enc28j60_state.mac_[3],
                enc28j60_state.mac_[4],
                enc28j60_state.mac_[5]);
            json["mac_address"] = mac_addr_str;
    
        }
        return json;
    }

};


struct wifi_settings {
    char ssid[32];
    char password[32];
    int auth_mode;    
    ipv4_settings ipv4;

    ArduinoJson::MyJsonDocument to_json() const {
        ArduinoJson::MyJsonDocument json;
        json["ssid"] = ssid;
        json["password"] = password;
        json["auth_mode"] = auth_mode;
        json["dhcp"] = ipv4.dhcp;
        json["ip"] = ipv4.ip.addr;
        json["nm"] = ipv4.nm.addr;
        json["gw"] = ipv4.gw.addr;
        json["dns"] = ipv4.dns.addr;

        uint8_t itf_sta_mac[6];
        cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, itf_sta_mac);
        char mac_addr_str[18];
        snprintf(
            mac_addr_str,
            sizeof mac_addr_str,
            "%02X:%02X:%02X:%02X:%02X:%02X\n",
            itf_sta_mac[0],
            itf_sta_mac[1],
            itf_sta_mac[2],
            itf_sta_mac[3],
            itf_sta_mac[4],
            itf_sta_mac[5]);
        json["mac_address"] = mac_addr_str;
            
        return json;
    }
    
};

struct mqtt_settings {
    char broker[32];
    uint16_t port;
    char username[32];
    char password[32];    

    ArduinoJson::MyJsonDocument to_json() const {
        ArduinoJson::MyJsonDocument json;
        json["broker"] = broker;
        json["port"] = port;
        json["username"] = username;
        json["password"] = password;
        return json;
    }
    
};

enum conn_type {
    WIFI,
    ETHERNET
};

struct client_mode_settings {
  public:
    enum conn_type conn_type;
    wifi_settings wifi;
    ethernet_settings eth;

    mqtt_settings mqtt;
    unsigned char password[8];
    
    sensor_settings_entry sensor_settings[MAX_SERIAL_SENSORS];

    ArduinoJson::MyJsonDocument to_json() const {
        ArduinoJson::MyJsonDocument json;
        json["conn_type"] = conn_type == WIFI ? "WIFI" : "ETHERNET";
        json["wifi"] = wifi.to_json();
        json["eth"] = eth.to_json();        
        json["mqtt"] = mqtt.to_json();
    
        auto sensor_settings_array = json["sensor_settings"].to<ArduinoJson::JsonArray>();
        for (auto &entry : sensor_settings) {
            sensor_settings_array.add(entry.to_json());
        }
    
        return json;
    }
    
};

union client_mode_settings_t {
    client_mode_settings settings;
    char padding[FLASH_SECTOR_SIZE];
} __attribute__((aligned(FLASH_SECTOR_SIZE)));

static_assert(sizeof(client_mode_settings_t) == FLASH_SECTOR_SIZE, "Size mismatch!");

const client_mode_settings *get_client_mode_settings();

void __not_in_flash_func(write_client_mode_settings)(void *param);

ArduinoJson::MyJsonDocument get_client_mode_settings_json();
