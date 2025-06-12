#pragma once
#include "enc28j60.h"
#include "lwip/err.h"
#include "lwip/netif.h"

err_t enc_driver_lwip_init();
err_t enc28j60_driver_os_init(ip4_addr_t ipaddr, ip4_addr_t netmask, ip4_addr_t gw);

inline netif net_if{};
