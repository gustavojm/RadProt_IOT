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

#include "debug.h"
#include "websocket.h"
#include "wifi_fns.h"
#include "ethernet_fns.h"
#include "watchdog.h"

#define MAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
#define WIFI_CONNECTION_MONITOR_DELAY_MS 1000 // 1 second
#define WIFI_RECONNECT_DELAY_MS 5000 // 5 seconds


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

void ws_message_handler(uint8_t *payload, uint32_t length, websocket_msg_type type) {
    lDebug(Info, "Websocket received: %.*s", length, payload);
}


static void main_task(__unused void *params) {

    if (cyw43_arch_init()) {
        lDebug(Error, "Failed to initialise Wi-Fi");
        return;
    }

    if (!enc28j60_state.init()) {
        lDebug(Error, "Failed to initialise ENC28J60 or not available");
    }    

    if (watchdog_enable_caused_reboot()) {
        // Turn on-board led to indicate reboot by watchdog timer expired
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        lDebug(Warn, "Rebooted by Watchdog!");
    }

    // Habilitar el watchdog con un tiempo máximo de 10s entre actualizaciones
    // watchdog_enable(10000, 1);

    cyw43_arch_enable_sta_mode();

    wifi_networks_scan();

    const ap_mode_settings *ap_settings = get_ap_mode_settings();
    const client_mode_settings *client_settings = get_client_mode_settings();

    if (strcmp(client_settings->wifi.ssid, "") == 0) {       // If no WiFi network to connect is defined
        initial_config = true;
    }

    httpd_init(ap_settings->hostname, ap_settings->domain_name);
    ws_server.init(ws_message_handler);

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

        netif_set_addr(netif, &addr, &mask, &gw);

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
        if (client_settings->conn_type == WIFI) {
            wifi_connect();
        } else {
            ethernet_connect();
        }
        
        mqtt_init();    
    }
 
    gpio_init(STATUS_LED_GPIO);
    gpio_set_dir(STATUS_LED_GPIO, true);

    if (client_settings->sensor_settings[0].enabled) {
        static Serial my_uart0(0, 0, 1, client_settings->sensor_settings[0].baudrate, SERIAL_BUFFERS_SIZE);
        my_uart0.init([]() { my_uart0.on_uart_rx(); });
        my_uart0.set_timeout(pdMS_TO_TICKS(100));
        my_uart0.set_delimiter('\r');
        static Sensor s0(my_uart0);
        s0.init();
    }

    if (client_settings->sensor_settings[1].enabled) {
        static Serial my_uart2(2, 2, 3, client_settings->sensor_settings[1].baudrate, SERIAL_BUFFERS_SIZE);
        my_uart2.init([]() { my_uart2.on_uart_rx(); });
        my_uart2.set_timeout(pdMS_TO_TICKS(100));
        my_uart2.set_delimiter('\r');
        static Sensor s2(my_uart2);
        s2.init();
    }

    if (client_settings->sensor_settings[2].enabled) {
        static Serial my_uart3(3, 6, 7, client_settings->sensor_settings[2].baudrate, SERIAL_BUFFERS_SIZE);
        my_uart3.init([]() { my_uart3.on_uart_rx(); });
        my_uart3.set_timeout(pdMS_TO_TICKS(100));
        my_uart3.set_delimiter('\r');
        static Sensor s3(my_uart3);
        s3.init();
    }
    
    while (true) {
        if (initial_config) {       // Blink the STATUS LED to indicate Initial Config Mode
            gpio_put(STATUS_LED_GPIO, true);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_put(STATUS_LED_GPIO, false);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {                    // Monitor WIFI connection and reconnect if necessary        
            if (client_settings->conn_type == WIFI && !(cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN)) {
                lDebug(Warn, "Wi-Fi disconnected! Attempting to reconnect...");
                netif_set_link_down(cyw43_state.netif);
                while(!wifi_connect()) {
                    vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
                }
            }
            vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECTION_MONITOR_DELAY_MS)); // Check connection status every second
        }
    }

    vTaskDelete(NULL);
}

/** 
 * Updates watchdog timer
 * Also, simulates sensor information sent periodically through UART
 */
void feedWatchdogTask(void *params) {
    // Set the TX and RX pins by using the function select on the GPIO
    gpio_set_function(4, GPIO_FUNC_UART);
    gpio_set_function(5, GPIO_FUNC_UART);
    int baud = 9600;
    uart_init(uart1, baud);
    vTaskDelay(1000);

    while (true) {
        uart_init(uart1, baud);
        vTaskDelay(100);        
        uart_puts(uart1, "Hel987.2233lo, UART!\r");
        vTaskDelay(100);
        uart_puts(uart1, "Mes12.34567890 from serial port!\r");
        vTaskDelay(100);
        uart_puts(uart1, "Est9999999999inta sentada en el verde limon\r");
        vTaskDelay(100);
        uart_puts(uart1, "& 000000 011 060 002 020  000000 496 0080A374F651 000340 103 6967\r");  // Real payload
        vTaskDelay(500);
        watchdog_update();
    }
}

int main(void) {
    stdio_init_all();
    TaskHandle_t task;
    s_PrintfSemaphore = xSemaphoreCreateMutex();

    xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE * 8, NULL, MAIN_TASK_PRIORITY, &task);

    xTaskCreate(feedWatchdogTask, "feedWdTask", 256, NULL, MAIN_TASK_PRIORITY, &feedWdTask_handle);
    vTaskCoreAffinitySet(feedWdTask_handle, 1 << 1);       // It's a mask, not a number of core

    gpio_init(INITIAL_CONFIG_GPIO);
    gpio_set_dir(INITIAL_CONFIG_GPIO, GPIO_IN);
    gpio_pull_up(INITIAL_CONFIG_GPIO);

    busy_wait_ms(10);
    initial_config = !gpio_get(INITIAL_CONFIG_GPIO);
     
    vTaskStartScheduler();
}