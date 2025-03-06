#include "settings.h"
#include <portmacro.h>
#include <string.h>
#include <hardware/watchdog.h>
#include <pico/cyw43_arch.h>

#include "FreeRTOS.h"
#include "task.h"

#include <pico/multicore.h>

const union {
    config_server_settings settings;
    char padding[FLASH_SECTOR_SIZE];
} __attribute__((aligned(FLASH_SECTOR_SIZE)))
s_Settings = { .settings = {
                   .ip = 0x017BA8C0,
                   .nm = 0x00FFFFFF,
                   .secondary_address =
                       0x006433c6, // TEST-NET-2. See the comment before 'secondary_address' definition for details.
                   .ssid = WIFI_SSID,
                   .password = WIFI_PASSWORD,
                   .hostname = "config",
                   .domain_name = "rad-prot.local",
                   .dns_ignores_network_suffix = true,
               } };

const config_server_settings *get_config_server_settings() {
    return &s_Settings.settings;
}

void write_config_server_settings(const config_server_settings *new_settings) {
    portENTER_CRITICAL();
    flash_range_erase((uint32_t)&s_Settings - XIP_BASE, FLASH_SECTOR_SIZE);
    flash_range_program((uint32_t)&s_Settings - XIP_BASE, (const uint8_t *)new_settings, sizeof(*new_settings));
    portEXIT_CRITICAL();
}

const char *get_next_domain_name_component(const char *domain_name, int *position, int *length) {
    if (!domain_name || !position || !length)
        return NULL;

    int pos = *position;
    const char *p = strchr(domain_name + pos, '.');
    if (p) {
        *position = p + 1 - domain_name;
        *length = p - domain_name - pos;
        return domain_name + pos;
    } else if (domain_name[pos]) {
        *length = strlen(domain_name + pos);
        *position = pos + *length;
        return domain_name + pos;
    } else
        return NULL;
}

//constexpr int padding_multiplier = (sizeof(client_settings) / FLASH_SECTOR_SIZE) + 1;
const client_settings_t s_Client_Settings = {                
                .settings = { 
                .wifi = {.ssid = "C14017750 7261",
                         .password = "malamala",
                         .auth_mode = CYW43_AUTH_WPA2_AES_PSK,
                         .dhcp = true,
                         .ip = 0xC889A8C0,        // 192.168.137.200                           
                         .nm = 0x00FFFFFF,  // 255.255.255.0
                         .gw = 0x0189A8C0,        // 192.168.137.1
                         .dns = 0x0189A8C0        // 192.168.137.1
                        },

                .mqtt = {.broker = "192.168.137.243",
                         .port = 1883,
                         .username = "jorgito",
                         .password = "pass_",
                        },

                .sensor_settings = { {.baudrate = 9600,
                                      .enabled = true,
                                      .publish_settings = { { .enabled = true,
                                                                .name = "H3",
                                                                .start = 3,
                                                                .end = 9,
                                                                .is_num = true,
                                                                .scale = 0.1,
                                                                .avg_cnt = 0,
                                                                .topic = "123" 
                                                            }, { 
                                                                .enabled = true,
                                                                .name = "H3AVG",
                                                                .start = 3,
                                                                .end = 9,
                                                                .is_num = true,
                                                                .scale = 0.1,
                                                                .avg_cnt = 5,
                                                                .topic = "r/123" 
                                                            }
                                                          },                                                                
									  },
                                      {.baudrate = 9600,
                                        .enabled = true,
                                        .publish_settings = { { .enabled = true,
                                                                  .name = "H3",
                                                                  .start = 3,
                                                                  .end = 9,
                                                                  .is_num = true,
                                                                  .scale = 0.1,
                                                                  .avg_cnt = 0,
                                                                  .topic = "456" 
                                                              }, { 
                                                                  .enabled = true,
                                                                  .name = "H3AVG",
                                                                  .start = 3,
                                                                  .end = 9,
                                                                  .is_num = true,
                                                                  .scale = 0.1,
                                                                  .avg_cnt = 5,
                                                                  .topic = "r/456" 
                                                              }
                                                            },                                                                
                                        },
                                        {.baudrate = 9600,
                                            .enabled = true,
                                            .publish_settings = { { .enabled = true,
                                                                      .name = "H3",
                                                                      .start = 3,
                                                                      .end = 9,
                                                                      .is_num = true,
                                                                      .scale = 0.1,
                                                                      .avg_cnt = 0,
                                                                      .topic = "789" 
                                                                  }, { 
                                                                      .enabled = true,
                                                                      .name = "H3AVG",
                                                                      .start = 3,
                                                                      .end = 9,
                                                                      .is_num = true,
                                                                      .scale = 0.1,
                                                                      .avg_cnt = 5,
                                                                      .topic = "r/789" 
                                                                  }
                                                                },                                                                
                                        }                                                                                
									} 
				}
					
};

const client_settings *get_client_settings() {
    return &s_Client_Settings.settings;
}

void __not_in_flash_func(write_client_settings)(void *param) {
    const client_settings_t *new_settings = static_cast<const client_settings_t *>(param);
    uint32_t start = (uint32_t)&s_Client_Settings - XIP_BASE;
    printf("Start: %i\n", start );
    printf("Size: %i\n", sizeof(client_settings_t));
   
    // Disable interrupts on the current core
    uint32_t status = save_and_disable_interrupts();

    // Perform flash write operation
    flash_range_erase((uint32_t)&s_Client_Settings - XIP_BASE, FLASH_SECTOR_SIZE);
    flash_range_program((uint32_t)&s_Client_Settings - XIP_BASE, (const uint8_t *)new_settings, FLASH_SECTOR_SIZE);

    // Re-enable interrupts on the current core
    restore_interrupts(status);    
    
    // #define AIRCR_Register (*((volatile uint32_t*)(PPB_BASE + 0x0ED0C)))
    // AIRCR_Register = 0x5FA0004;
    
}

ArduinoJson::MyJsonDocument get_client_settings_json() {
    return s_Client_Settings.settings.to_json();
};