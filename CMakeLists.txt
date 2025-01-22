cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD pico_w)

# Include build functions from Pico SDK
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/pico_sdk_import.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FreeRTOS_Kernel_import.cmake)

pico_sdk_init()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

project(PicoHTTPServer C CXX ASM)

include(FetchContent)
FetchContent_Declare(ArduinoJson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG        v7.0.4
)
FetchContent_MakeAvailable(ArduinoJson)

add_compile_definitions(ENC_DEBUG_ON=0 NO_SYS=0)

add_custom_target(${name}-fs DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)
        
add_library(patched_lwip STATIC
    patched_lwip/ip4.c
)

target_include_directories(patched_lwip PUBLIC
    ../pico-sdk/lib/lwip/src/include
    inc
)

add_executable(PicoHTTPServer
        main.c
        dhcpserver/dhcpserver.c
        dns/dnsserver.c
        #httpserver.c
        src/server_settings.c
        src/tcp_server.cpp
        src/tcp_server_command.cpp
        src/httpd.c
        src/fs.c
        src/hooks.c
        )

target_compile_definitions(PicoHTTPServer PRIVATE
        WIFI_SSID="PICONET"
        WIFI_PASSWORD="picopico"
        NO_SYS=0)
        
target_include_directories(PicoHTTPServer PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}
        inc)

target_link_libraries(PicoHTTPServer        
        patched_lwip
        pico_cyw43_arch_lwip_sys_freertos
        pico_stdlib
        pico_lwip_iperf
        FreeRTOS-Kernel-Heap4
        ArduinoJson
        )

if (TARGET Profiler)
    target_link_libraries(PicoHTTPServer 
            Profiler)
endif()

pico_add_extra_outputs(PicoHTTPServer)

# disable usb output, disable uart output
pico_enable_stdio_usb(PicoHTTPServer 1)
pico_enable_stdio_uart(PicoHTTPServer 0)
