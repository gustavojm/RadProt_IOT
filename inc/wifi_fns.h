#pragma once

#include <pico/cyw43_arch.h>
#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include <lwip/dns.h>

#include "debug.h"
#include "settings.h"

void wifi_networks_scan(bool active = true);

bool wifi_connect();

int scan_auth_mode_to_connect_auth_mode(int scan_auth_mode);

int connect_auth_mode_to_scan_auth_mode(int connect_auth_mode);
