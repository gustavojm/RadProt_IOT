#include <hardware/watchdog.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <stdarg.h>

#include <lwip/dns.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "dhcpserver/dhcpserver.h"
#include "dns/dnsserver.h"
#include "httpd.h"
#include "settings.h"

#include "MQTT.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "sensor.h"
#include "serial.h"
#include <pico/flash.h>
#include <pico/multicore.h>

#include "debug_printf.h"
#include "websocket.h"

#define MAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
#define RECONNECT_DELAY_MS 5000 // 5 seconds
#define MAX_RETRIES        5    // Maximum retry attempts

void connect_to_wifi() {
    int retries = 0;

    while (retries < MAX_RETRIES) {
        printf("Connecting to Wi-Fi... Attempt %d\n", retries + 1);
        const client_mode_settings *client_settings = get_client_mode_settings();

        // Attempt to connect to Wi-Fi
        if (cyw43_arch_wifi_connect_timeout_ms(
                client_settings->wifi.ssid, client_settings->wifi.password, client_settings->wifi.auth_mode, 30000) == 0) {
            // if (cyw43_arch_wifi_connect_timeout_ms("Redmi", "peperina", CYW43_AUTH_WPA2_MIXED_PSK,
            //     30000) == 0) {
            if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN) {
                printf("Connected to Wi-Fi successfully!\n");

                if (client_settings->wifi.dhcp) {
                    // Wait for DHCP to assign an IP
                    while (netif_default->ip_addr.addr == 0) {
                        printf("Waiting for DHCP...\n");
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
                    printf("Static IP set to: %s\n", ip_addr);
                }

                printf("Connected! IP Address: %s\n", ip4addr_ntoa(&netif_default->ip_addr));

                return;
            }
        }

        printf("Failed to connect. Retrying in %d ms...\n", RECONNECT_DELAY_MS);
        vTaskDelay(pdMS_TO_TICKS(RECONNECT_DELAY_MS));
        retries++;
    }

    printf("Failed to connect after %d attempts. Giving up.\n", MAX_RETRIES);
}

static void set_secondary_ip_address(int address) {
    /************************************ !!! WARNING !!! ************************************
     * If you get an 'undefined reference to ip4_secondary_ip_address' error here,			 *
     * you need to patch your lwIP using the lwip_patch/lwip.patch file from this repository.*
     * This ensures that this device can pretend to be a router redirecting requests to		 *
     * external IPs to its login page, so the OS can automatically navigate there.			 *
     *****************************************************************************************/

    extern int ip4_secondary_ip_address;
    ip4_secondary_ip_address = address;
}

static int wifi_scan_cb(void *env, const cyw43_ev_scan_result_t *result) {
    if (result) {
        auto result_ins = wifi_networks.insert(*result);
    }
    return 0;
}

void ws_message_handler(uint8_t *data, uint32_t len, ws_type_t type) {
    printf("Websocket received: %.*s", len, data);
}

static void main_task(__unused void *params) {

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return;
    }

    cyw43_arch_enable_sta_mode();

    cyw43_wifi_scan_options_t scan_options = { 0 };
    int err = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, wifi_scan_cb);
    if (err == 0) {
        printf("\nPerforming wifi scan\n");
    } else {
        printf("Failed to start scan: %d\n", err);
    }
    while (cyw43_wifi_scan_active(&cyw43_state)) {
        vTaskDelay(1000);
    }

    printf("WIFI Scan finished\n");

    printf("Detected WIFI Networks: \n");

    for (auto wifi_net : wifi_networks) {
        printf("ssid: %s, signal: %i channel: %i bssid: ", wifi_net.ssid, wifi_net.rssi, wifi_net.channel);
        for (int i = 0; i < 6; i++) {
            printf("%02x", wifi_net.bssid[i]);
            if (i < 5) {
                printf(":");
            }
        }
        printf("\n");
    }

    const ap_mode_settings *ap_settings = get_ap_mode_settings();
    const client_mode_settings *client_settings = get_client_mode_settings();

    if (strcmp(client_settings->wifi.ssid, "") == 0) {       // If no WiFi network to connect is defined
        initial_config = true;
    }

    if (initial_config) {
        uint8_t itf_sta_mac[6];
        cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, itf_sta_mac);

        char ssid[32];
        snprintf(ssid, sizeof(ssid), "%s-%02X", ap_settings->ssid, itf_sta_mac[5]);

        cyw43_arch_enable_ap_mode(
            ssid, ap_settings->password, ap_settings->password[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN);

        struct netif *netif = netif_default;
        ip4_addr_t addr = { .addr = ap_settings->ip };
        ip4_addr_t mask = { .addr = ap_settings->nm };
        ip4_addr_t gw = { .addr = ap_settings->ip};

        netif_set_addr(netif, &addr, &mask, &addr);

        // Start the dhcp server
        static dhcp_server_t dhcp_server;
        dhcp_server_init(&dhcp_server, &netif->ip_addr, &netif->netmask, ap_settings->domain_name);
        dns_server_init(
            netif->ip_addr.addr,
            ap_settings->secondary_address,
            ap_settings->hostname,
            ap_settings->domain_name,
            ap_settings->dns_ignores_network_suffix);
        set_secondary_ip_address(ap_settings->secondary_address);
    } else {
        connect_to_wifi();  
        mqtt_init();    
    }

    gpio_init(STATUS_LED_GPIO);
    gpio_set_dir(STATUS_LED_GPIO, true);

    if (client_settings->sensor_settings[0].enabled) {
        static Serial my_uart0(0, 1, 2, 9600, SERIAL_BUFFERS_SIZE);
        my_uart0.init([]() { my_uart0.on_uart_rx(); });
        my_uart0.set_timeout(pdMS_TO_TICKS(100));
        my_uart0.set_delimiter('\n');
        static Sensor s0(my_uart0);
        s0.init();
    }

    if (client_settings->sensor_settings[1].enabled) {
        static Serial my_uart2(2, 2, 3, 9600, SERIAL_BUFFERS_SIZE);
        my_uart2.init([]() { my_uart2.on_uart_rx(); });
        my_uart2.set_timeout(pdMS_TO_TICKS(100));
        my_uart2.set_delimiter('\n');
        static Sensor s2(my_uart2);
        s2.init();
    }

    if (client_settings->sensor_settings[2].enabled) {
        static Serial my_uart3(3, 6, 7, 9600, SERIAL_BUFFERS_SIZE);
        my_uart3.init([]() { my_uart3.on_uart_rx(); });
        my_uart3.set_timeout(pdMS_TO_TICKS(100));
        my_uart3.set_delimiter('\n');
        static Sensor s3(my_uart3);
        s3.init();
    }

    httpd_init(ap_settings->hostname, ap_settings->domain_name);
    ws_server_t ws_server;
    ws_server.msg_handler = ws_message_handler;

    ws_server_init(&ws_server);

    if (initial_config) {
        while (true) {
            gpio_put(STATUS_LED_GPIO, true);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_put(STATUS_LED_GPIO, false);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    } else {
        // Monitor connection and reconnect if necessary
        while (true) {
            if (!(cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN)) {

                printf("Wi-Fi disconnected! Attempting to reconnect...\n");
                netif_set_link_down(cyw43_state.netif);
                connect_to_wifi();
            }
            vTaskDelay(pdMS_TO_TICKS(1000)); // Check connection status every second
        }
    }

    vTaskDelete(NULL);
}

void writeStringTask(void *params) {
    // flash_safe_execute_core_init();

    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(4, GPIO_FUNC_UART);
    gpio_set_function(5, GPIO_FUNC_UART);
    uart_init(uart1, 9600);

    vTaskDelay(5 * 1000);

    while (1) {
        uart_init(uart1, 9600);
        vTaskDelay(500);
        // Send out a string, with CR/LF conversions
        uart_puts(uart1, "Hel987.2233lo, UART!\n");
        vTaskDelay(500);
        uart_puts(uart1, "Mes12.34567890 from serial port!\n");
        vTaskDelay(500);
        uart_puts(uart1, "Est9999999999inta sentada en el verde limon\n");
        vTaskDelay(500);
    }
}

int main(void) {
    stdio_init_all();
    TaskHandle_t task;
    s_PrintfSemaphore = xSemaphoreCreateMutex();

    xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE * 2, NULL, MAIN_TASK_PRIORITY, &task);

    TaskHandle_t writeStringTask_handle;
    xTaskCreate(writeStringTask, "WriteStringTask", 256, NULL, MAIN_TASK_PRIORITY, &writeStringTask_handle);
    vTaskCoreAffinitySet(writeStringTask_handle, 1);

    gpio_init(INITIAL_CONFIG_GPIO);
    gpio_set_dir(INITIAL_CONFIG_GPIO, GPIO_IN);
    gpio_pull_up(INITIAL_CONFIG_GPIO);

    busy_wait_ms(10);
    initial_config = !gpio_get(INITIAL_CONFIG_GPIO);
    vTaskStartScheduler();
}