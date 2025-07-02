#pragma once

#include <pico/cyw43_arch.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include <lwip/dns.h>

#include "debug.h"
#include "settings.h"

#include "enc28j60.h"
#include "enc28j60_LWIP_FreeRTOS.h"

void ethernet_connect();
