#include "wifi_fns.h"

#define RECONNECT_DELAY_MS 5000 // 5 seconds
#define MAX_RETRIES        5    // Maximum retry attempts

static int wifi_scan_cb(void *env, const cyw43_ev_scan_result_t *result) {
    if (result) {
        auto result_ins = wifi_networks.insert(*result);
    }
    return 0;
}

void wifi_networks_scan(bool active) {
    cyw43_wifi_scan_options_t scan_options = { 0 };
    int err = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, wifi_scan_cb);
    if (err == 0) {
        lDebug(Info, "Performing wifi scan");
    } else {
        lDebug(Error, "Failed to start scan: %d", err);
    }
    while (active && cyw43_wifi_scan_active(&cyw43_state)) {
        vTaskDelay(1000);
    }

    lDebug(Info, "WIFI Scan finished");

    lDebug(Info, "Detected WIFI Networks: ");

    for (auto wifi_net : wifi_networks) {
        debugPrintf("ssid: %s, signal: %i channel: %i bssid: ", wifi_net.ssid, wifi_net.rssi, wifi_net.channel);
        for (int i = 0; i < 6; i++) {
            debugPrintf("%02x", wifi_net.bssid[i]);
            if (i < 5) {
                debugPrintf(":");
            }
        }
        debugPrintf("\n");
    }
}

void wifi_connect() {
    lDebug(Info, "Connecting to Wi-Fi...");
    const client_mode_settings *client_settings = get_client_mode_settings();

    // Attempt to connect to Wi-Fi
    if (cyw43_arch_wifi_connect_timeout_ms(
            client_settings->wifi.ssid, client_settings->wifi.password, client_settings->wifi.auth_mode, 30000) == 0) {
        // if (cyw43_arch_wifi_connect_timeout_ms("Redmi", "peperina", CYW43_AUTH_WPA2_MIXED_PSK,
        //     30000) == 0) {
        if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN) {
            lDebug(Info, "Connected to Wi-Fi successfully!");

            if (client_settings->wifi.dhcp) {
                // Wait for DHCP to assign an IP
                while (netif_default->ip_addr.addr == 0) {
                    lDebug(Info, "Waiting for DHCP...");
                    sleep_ms(1000);
                }
            } else {
                cyw43_arch_lwip_begin();
                dhcp_stop(cyw43_state.netif); // turn off DHCP
                netif_set_addr(
                    cyw43_state.netif, &client_settings->wifi.ip, &client_settings->wifi.nm, &client_settings->wifi.gw);
                dns_setserver(0, &client_settings->wifi.dns); // Set primary DNS
                char *ip_addr = ip4addr_ntoa(&client_settings->wifi.ip);
                cyw43_arch_lwip_end();
                lDebug(Info, "Static IP set to: %s", ip_addr);
            }

            lDebug(Info, "Connected! IP Address: %s", ip4addr_ntoa(&netif_default->ip_addr));

            return;
        }
    }

    lDebug(Warn, "Failed to connect. Retrying in %d ms...\n", RECONNECT_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
}



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
    case CYW43_AUTH_WPA_TKIP_PSK: scan_auth_mode = 1; break;
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
