#include <hardware/watchdog.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <stdarg.h>

#include <lwip/ip4_addr.h>
#include <lwip/netif.h>
#include <lwip/dns.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "dhcpserver/dhcpserver.h"
#include "dns/dnsserver.h"
#include "httpd.h"
#include "settings.h"

#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "serial.h"
#include "ssi.h"
#include "mqtt.h"
#include "sensor.h"

#include "debug_printf.h"

#define MAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)
#define RECONNECT_DELAY_MS 5000 // 5 seconds
#define MAX_RETRIES 5           // Maximum retry attempts

void connect_to_wifi() {
    int retries = 0;

    while (retries < MAX_RETRIES) {
        printf("Connecting to Wi-Fi... Attempt %d\n", retries + 1);
        const client_settings *client_settings = get_client_settings();


        // Attempt to connect to Wi-Fi
        if (cyw43_arch_wifi_connect_timeout_ms("C14017750 7261", "malamala", CYW43_AUTH_WPA2_AES_PSK,
                                               30000) == 0) {
            if (cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN) {
                printf("Connected to Wi-Fi successfully!\n");

                if (!client_settings->wifi.dhcp) {                  
                    //cyw43_arch_enable_sta_mode();
                    cyw43_arch_lwip_begin();
                    dhcp_stop(cyw43_state.netif);     // turn off DHCP
                    netif_set_addr(cyw43_state.netif, &client_settings->wifi.ip, &client_settings->wifi.net_mask, &client_settings->wifi.gw);
                    dns_setserver(0, &client_settings->wifi.dns); // Set primary DNS    
                    char *ip_addr = ip4addr_ntoa(&client_settings->wifi.ip);
                    cyw43_arch_lwip_end();
                    printf("Static IP set to: %s\n", ip_addr);
                }
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

static int scan_result(void *env, const cyw43_ev_scan_result_t *result) {
    if (result) {
        auto result_ins = wifi_networks.insert(*result);
    }
    return 0;
}

static void main_task(__unused void *params) {

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return;
    }

    cyw43_arch_enable_sta_mode();

    cyw43_wifi_scan_options_t scan_options = { 0 };
    int err = cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, scan_result);
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

    // printf("MY MAC ADDRESS: %02x:%02x:%02x:%02x:%02x:%02x\n",
    // itf_sta_mac[0], itf_sta_mac[1], itf_sta_mac[2], itf_sta_mac[3], itf_sta_mac[4], itf_sta_mac[5]);

    const config_server_settings *settings = get_config_server_settings();

    cyw43_arch_enable_ap_mode(
        settings->ssid,
        settings->password,
        settings->password[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN);

    struct netif *netif = netif_default;
    ip4_addr_t addr = { .addr = settings->ip }, mask = { .addr = settings->net_mask };

    netif_set_addr(netif, &addr, &mask, &addr);

    // Start the dhcp server
    static dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &netif->ip_addr, &netif->netmask, settings->domain_name);
    dns_server_init(
        netif->ip_addr.addr,
        settings->secondary_address,
        settings->hostname,
        settings->domain_name,
        settings->dns_ignores_network_suffix);
    set_secondary_ip_address(settings->secondary_address);

    connect_to_wifi();

    httpd_init(settings->hostname, settings->domain_name);
    ssi_init();

    static Serial my_uart0(uart0, 1, 2, 9600, SERIAL_BUFFERS_SIZE);
    my_uart0.init([]() {my_uart0.on_uart_rx(); });
    my_uart0.set_timeout(pdMS_TO_TICKS(100));
    my_uart0.set_delimiter('\n');

    static Sensor s0(my_uart0, &(get_client_settings()->sensor_settings)[0]);    
    s0.init();
    mqtt_init();

    // Monitor connection and reconnect if necessary
    while (true) {
        if (!(cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN)) {

            printf("Wi-Fi disconnected! Attempting to reconnect...\n");
            connect_to_wifi();
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check connection status every second
    }

    vTaskDelete(NULL);
}

void writeStringTask(void *params) {
    // Set the TX and RX pins by using the function select on the GPIO
    // Set datasheet for more information on function select
    gpio_set_function(4, GPIO_FUNC_UART);
    gpio_set_function(5, GPIO_FUNC_UART);
    while (1) {

        uart_init(uart1, 9600);

        // Send out a string, with CR/LF conversions
        // uart_puts(uart1, "Hel987.2233lo, UART!\n");
        // vTaskDelay(1000);
        uart_puts(uart1, "Mes12.34567890 from serial port!\n");
        vTaskDelay(3000);
        // uart_puts(uart1, "Est9999999999inta sentada en el verde limon\n");
        // vTaskDelay(1000);

    }

}

int main(void) {
    stdio_init_all();
    TaskHandle_t task;
    s_PrintfSemaphore = xSemaphoreCreateMutex();

    xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE, NULL, MAIN_TASK_PRIORITY, &task);    
    xTaskCreate(writeStringTask, "WriteStringTask", 256, NULL, 1, NULL);

    vTaskStartScheduler();
}