#include "enc28j60_LWIP_FreeRTOS.h"
#include "enc28j60.h"
#include "lwip/err.h"

#include "lwip/dhcp.h"
#include "lwip/inet.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/stats.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"

#include "FreeRTOS.h"
#include "pico/stdlib.h"
#include "semphr.h"
#include "task.h"
#include <cstring>

#include "gpio.h"
#include "spi.h"
#include "utils.h"

#ifdef ENC_DEBUG_ON
#include <pico/stdio.h>
#endif

constexpr uint32_t NETWORKING_CORE_ID = 1 << 1; // pin all networking functions to core1

static void tcpip_init_done(void *arg) { xSemaphoreGive(static_cast<SemaphoreHandle_t>(arg)); }

static void netif_status_callback(struct netif *netif) {
    printf("NETIF status changed %s\n", ip4addr_ntoa(netif_ip4_addr(netif)));
}

static void netif_link_callback(struct netif *netif) {
    drivers::enc28j60 *me = static_cast<drivers::enc28j60 *>(netif->state);
    printf("NETIF link changed: ");
    if (me->is_link_up()) {
        printf("LINK IS UP!\n");
    } else {
        printf("LINK IS DOWN!\n");
    }
}

err_t enc28j60_driver_os_init(ip4_addr_t ipaddr, ip4_addr_t netmask, ip4_addr_t gw) {

    if (!enc28j60_state.is_available) {
        return ERR_ABRT;
    }    

    if (netif_add(&enc28j60_state.netif, &ipaddr, &netmask, &gw, static_cast<void *>(&enc28j60_state),
                  drivers::enc28j60::eth_netif_init, tcpip_input) == nullptr) {
        printf("netif_add failed\n");
        return ERR_ABRT;
    }

    printf("netif_add ADDED\n");

    enc28j60_state.netif.name[0] = 'e';
    enc28j60_state.netif.name[1] = '0';

    netif_set_status_callback(&enc28j60_state.netif, netif_status_callback);
    netif_set_link_callback(&enc28j60_state.netif, netif_link_callback);
    netif_set_hostname(&enc28j60_state.netif, "PICO");

    netif_set_default(&enc28j60_state.netif);
    netif_set_up(&enc28j60_state.netif);
    // dhcp_start(&eth_driver.net_if);
    // printf("netif DHCP STARTED\n");

    // tcpip_init allready called by cyw43 driver
    // SemaphoreHandle_t init_sem = xSemaphoreCreateBinary();
    // tcpip_init(tcpip_init_done, init_sem);
    // xSemaphoreTake(init_sem, portMAX_DELAY);

    irq_loop_sem = xSemaphoreCreateBinary();
    if (!irq_loop_sem) {
        printf("Failed to create irq_loop semaphore\n");
        return ERR_ABRT;
    }

    TaskHandle_t irq_loop_task_handle{};
    if (xTaskCreate([](void *me) { static_cast<drivers::enc28j60 *>(me)->irq_deferred_handler(); },
                    "irq_loop", 4096, (void *)&enc28j60_state, configMAX_PRIORITIES - 1,
                    &irq_loop_task_handle) != pdPASS) {
        return ERR_ABRT;
    }

#if configUSE_CORE_AFFINITY && configNUMBER_OF_CORES > 1
    vTaskCoreAffinitySet(irq_loop_task_handle, NETWORKING_CORE_ID);
#endif

    enc28j60_state.enable_interupts();

    return ERR_OK;
}
