#include <stdarg.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <hardware/watchdog.h>

#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "dhcpserver/dhcpserver.h"
#include "dns/dnsserver.h"
#include "server_settings.h"
#include "httpd.h"

#include "hardware/vreg.h"
#include "hardware/clocks.h"
#include "ssi.h"

#include "debug_printf.h"

#define MAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 2UL)

static void set_secondary_ip_address(int address)
{
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

static void main_task(__unused void *params)
{
	
	if (cyw43_arch_init())
	{
		printf("failed to initialise\n");
		return;
	}


    cyw43_arch_enable_sta_mode();
	
	cyw43_wifi_scan_options_t scan_options = {0};
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

	for (auto wifi_net: wifi_networks) {		
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

	const pico_server_settings *settings = get_pico_server_settings();

	cyw43_arch_enable_ap_mode(settings->network_name, settings->network_password, settings->network_password[0] ? CYW43_AUTH_WPA2_MIXED_PSK : CYW43_AUTH_OPEN);

	struct netif *netif = netif_default;
	ip4_addr_t addr = { .addr = settings->ip_address }, mask = { .addr = settings->network_mask };
	
	netif_set_addr(netif, &addr, &mask, &addr);
	
	// Start the dhcp server
	static dhcp_server_t dhcp_server;
	dhcp_server_init(&dhcp_server, &netif->ip_addr, &netif->netmask, settings->domain_name);
	dns_server_init(netif->ip_addr.addr, settings->secondary_address, settings->hostname, settings->domain_name, settings->dns_ignores_network_suffix);
	set_secondary_ip_address(settings->secondary_address);

    httpd_init(settings->hostname, settings->domain_name);
	ssi_init();

	vTaskDelete(NULL);
}

int main(void)
{
	stdio_init_all();
	TaskHandle_t task;
	s_PrintfSemaphore = xSemaphoreCreateMutex();
	xTaskCreate(main_task, "MainThread", configMINIMAL_STACK_SIZE, NULL, MAIN_TASK_PRIORITY, &task);
	vTaskStartScheduler();
}