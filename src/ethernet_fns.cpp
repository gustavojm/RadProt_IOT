#include "ethernet_fns.h"


void ethernet_connect() {
    lDebug(Info, "Enabling Ethernet...");
    const client_mode_settings *client_settings = get_client_mode_settings();

    // Allways initialize after cyw43, to use the tcpip_thread created by it       

    if (client_settings->eth.ipv4.dhcp) {
        ip4_addr_t ipaddr, netmask, gw;
        IP4_ADDR(&ipaddr, 0, 0, 0, 0);
        IP4_ADDR(&netmask, 0, 0, 0, 0);
        IP4_ADDR(&gw, 0, 0, 0, 0);
        enc28j60_driver_os_init(ipaddr, netmask, gw);
        dhcp_start(&enc28j60_state.netif);
        lDebug(Info, "Wait for DHCP to assign an IP");
        while (enc28j60_state.netif.ip_addr.addr == 0) {
            lDebug(Info, "Waiting for DHCP...");
            sleep_ms(1000);
        }
        lDebug(Info, "Connected! IP Address: %s", ip4addr_ntoa(netif_ip4_addr(&enc28j60_state.netif)));
    } else {
        enc28j60_driver_os_init(client_settings->eth.ipv4.ip, client_settings->eth.ipv4.nm, client_settings->eth.ipv4.gw);
        lDebug(Info, "Static IP set to: %s", &enc28j60_state.netif.ip_addr);
    }

    
}
