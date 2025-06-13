#include "enc28j60.h"
#include "enc28j60_LWIP_FreeRTOS.h"

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

void ws_message_handler(uint8_t *data, uint32_t len, ws_type_t type) {
    lDebug(Info, "Websocket received: %.*s", len, data);
}


void ethernet_connect() {
    lDebug(Info, "Enabling Ethernet...");
    const client_mode_settings *client_settings = get_client_mode_settings();

    // Allways initialize after cyw43, to use the tcpip_thread created by it
    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 192, 168, 2, 25);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 2, 1);
    
    //enc28j60_driver_os_init(client_settings->eth.ipv4.ip, client_settings->eth.ipv4.nm, client_settings->eth.ipv4.gw);
    enc28j60_driver_os_init(ipaddr, netmask, gw);


}



static void main_task(__unused void *params) {

    if (cyw43_arch_init()) {
        lDebug(Error, "failed to initialise");
        return;
    }

    cyw43_arch_enable_sta_mode();

    wifi_networks_scan();

    const ap_mode_settings *ap_settings = get_ap_mode_settings();
    const client_mode_settings *client_settings = get_client_mode_settings();

    if (strcmp(client_settings->wifi.ssid, "") == 0) {       // If no WiFi network to connect is defined
        initial_config = true;
    }

    ws_server_t ws_server;
    ws_server.msg_handler = ws_message_handler;
    httpd_init(ap_settings->hostname, ap_settings->domain_name);

    ws_server_init(&ws_server);   

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
        if (client_settings->wifi.enabled) {
            wifi_connect();
        }
        
        if (client_settings->eth.enabled) {
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
        my_uart0.set_delimiter('\n');
        static Sensor s0(my_uart0);
        s0.init();
    }

    if (client_settings->sensor_settings[1].enabled) {
        static Serial my_uart2(2, 2, 3, client_settings->sensor_settings[1].baudrate, SERIAL_BUFFERS_SIZE);
        my_uart2.init([]() { my_uart2.on_uart_rx(); });
        my_uart2.set_timeout(pdMS_TO_TICKS(100));
        my_uart2.set_delimiter('\n');
        static Sensor s2(my_uart2);
        s2.init();
    }

    if (client_settings->sensor_settings[2].enabled) {
        static Serial my_uart3(3, 6, 7, client_settings->sensor_settings[2].baudrate, SERIAL_BUFFERS_SIZE);
        my_uart3.init([]() { my_uart3.on_uart_rx(); });
        my_uart3.set_timeout(pdMS_TO_TICKS(100));
        my_uart3.set_delimiter('\n');
        static Sensor s3(my_uart3);
        s3.init();
    }
    
    if (initial_config) {
        while (true) {
            gpio_put(STATUS_LED_GPIO, true);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_put(STATUS_LED_GPIO, false);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    } else {
        // Monitor connection and reconnect if necessary
    //     while (true) {
    //         if (client_settings->wifi.enabled && !(cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN)) {

    //             lDebug(Warn, "Wi-Fi disconnected! Attempting to reconnect...");
    //             netif_set_link_down(cyw43_state.netif);
    //             while(!wifi_connect()) {
    //                 vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_DELAY_MS));
    //             }          
    //             ws_request_restart();                     
    //         }
    //         vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECTION_MONITOR_DELAY_MS)); // Check connection status every second
    //     }
        
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECTION_MONITOR_DELAY_MS)); // Check connection status every second
    }

    vTaskDelete(NULL);
}

/** 
 * Simulates sensor information sent periodically through UART
 */
void writeStringTask(void *params) {
    // Set the TX and RX pins by using the function select on the GPIO
    gpio_set_function(4, GPIO_FUNC_UART);
    gpio_set_function(5, GPIO_FUNC_UART);
    int baud = 9600;
    uart_init(uart1, baud);
    vTaskDelay(1000);

    while (true) {
        uart_init(uart1, baud);
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

    xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE * 8, NULL, MAIN_TASK_PRIORITY, &task);

    TaskHandle_t writeStringTask_handle;
    xTaskCreate(writeStringTask, "WriteStringTask", 256, NULL, MAIN_TASK_PRIORITY, &writeStringTask_handle);
    vTaskCoreAffinitySet(writeStringTask_handle, 1 << 1);       // It's a mask, not a number of core

    gpio_init(INITIAL_CONFIG_GPIO);
    gpio_set_dir(INITIAL_CONFIG_GPIO, GPIO_IN);
    gpio_pull_up(INITIAL_CONFIG_GPIO);

    busy_wait_ms(10);
    initial_config = !gpio_get(INITIAL_CONFIG_GPIO);
    vTaskStartScheduler();
}