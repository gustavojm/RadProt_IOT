#pragma once
#include "enc28j60.h"
#include "lwip/err.h"
#include "lwip/netif.h"

err_t enc28j60_driver_os_init(ip4_addr_t ipaddr, ip4_addr_t netmask, ip4_addr_t gw);


inline drivers::Spi spi0_{{.spi_handle = spi0, 
    .CLK_gpio = 18, 
    .MOSI_gpio = 19, 
    .MISO_gpio = 16, 
    .baudrate_Hz = 12 * 1000000
   }};

inline drivers::enc28j60 enc28j60_state{{.CS_gpio = 17, 
              .RST_gpio = 20, 
              .IRQ_gpio = 21, 
              .spi = spi0_
             }};
