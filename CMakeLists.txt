cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD pico_w)

# Include build functions from Pico SDK

set(PICO_SDK_PATH ./pico-sdk)
set(FREERTOS_KERNEL_PATH ../../FreeRTOS-Kernel)

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FreeRTOS_Kernel_import.cmake)

set(SKIP_PICO_MALLOC 1)

pico_sdk_init()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

project(RadProt_IoT C CXX ASM)

set(FIRMWARE_VERSION "v1.2.2")

#
# Static analyzers
#
# Currently supporting: Clang-Tidy, Cppcheck.

option(${PROJECT_NAME}_ENABLE_CLANG_TIDY "Enable static analysis with Clang-Tidy." OFF)
option(${PROJECT_NAME}_ENABLE_CPPCHECK "Enable static analysis with Cppcheck." OFF)
include(cmake/StaticAnalyzers.cmake)

set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS}
                    -Wno-volatile 
                    -ffile-prefix-map=${CMAKE_SOURCE_DIR}/=
   )

list(JOIN CMAKE_CXX_FLAGS " " CMAKE_CXX_FLAGS)

include(FetchContent)

FetchContent_Declare(ArduinoJson
    #GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    SOURCE_DIR ../deps/arduinojson-src
    GIT_TAG        v7.0.4
)
FetchContent_MakeAvailable(ArduinoJson)


FetchContent_Declare(
    etl
    #GIT_REPOSITORY https://github.com/ETLCPP/etl.git
    SOURCE_DIR ../deps/etl-src
    GIT_TAG master  # You can replace 'master' with a specific version tag
)
FetchContent_MakeAvailable(etl)

add_subdirectory(enc28j60)
add_subdirectory(hal)

add_custom_target(${name}-fs DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)

add_custom_target(makefsdata
  COMMAND ../httpserver/makefsdata/build/makefsdata ${CMAKE_CURRENT_SOURCE_DIR}/httpserver/fs -f:${CMAKE_CURRENT_SOURCE_DIR}/httpserver/fsdata.c
  COMMENT "Running makefsdata"
)

add_executable(${PROJECT_NAME}
        src/main.cpp
        dhcpserver/dhcpserver.cpp
        dns/dnsserver.cpp
        httpserver/httpd.cpp
        httpserver/fs.cpp
        src/c_cpp_config.cpp
        src/settings.cpp
        src/hooks.cpp
        src/post.cpp
        src/firmware_update.cpp
        hal/src/serial.cpp
        hal/src/spi.cpp
        hal/src/gpio.cpp        
        src/sensor.cpp
        src/FreeRTOSTimers.cpp
        src/MQTT.cpp
        src/wifi_fns.cpp
        src/ethernet_fns.cpp
        src/websocket.cpp
        src/crypto.cpp
        src/debug.cpp
        src/status.cpp
        paho.mqtt.embedded-c/MQTTPacket/src/MQTTPacket.cpp
        paho.mqtt.embedded-c/MQTTPacket/src/MQTTDeserializePublish.cpp
        paho.mqtt.embedded-c/MQTTPacket/src/MQTTConnectClient.cpp
        paho.mqtt.embedded-c/MQTTPacket/src/MQTTSerializePublish.cpp
        paho.mqtt.embedded-c/MQTTPacket/src/MQTTSubscribeClient.cpp
        paho.mqtt.embedded-c/MQTTClient-C/src/FreeRTOS/MQTTFreeRTOS.cpp
        paho.mqtt.embedded-c/MQTTClient-C/src/MQTTClient.cpp
        )

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/uart_rx.pio)

target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIFI_SSID="RadProt_IoT"
        WIFI_PASSWORD="NASA1234"
        FIRMWARE_VERSION="${FIRMWARE_VERSION}"
        NO_SYS=0
        ETL_NO_STL
        ETL_NO_INITIALIZER_LIST
        ARDUINOJSON_ENABLE_STD_STREAM=0
        PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1
        LWIP_DEBUG=1        
        PICO_MAX_SHARED_IRQ_HANDLERS=8
        #NDEBUG
        )


target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}
        inc
        hal/inc
        enc28j60/inc
        httpserver
        ${etl_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/paho.mqtt.embedded-c/MQTTClient-C/src
        ${CMAKE_SOURCE_DIR}/paho.mqtt.embedded-c/MQTTPacket/src    
        ${PICO_SDK_PATH}/lib/mbedtls
        )

target_link_libraries(${PROJECT_NAME}        
        pico_cyw43_arch_lwip_sys_freertos
        pico_stdlib
        pico_mbedtls
        hardware_pio
        hardware_uart
        hardware_flash
        hardware_spi
        FreeRTOS-Kernel-Heap4
        ArduinoJson
        etl::etl        
        enc28j60_driver
        hal
        )

if (TARGET Profiler)
    target_link_libraries(${PROJECT_NAME} 
            Profiler)
endif()

add_dependencies(${PROJECT_NAME} makefsdata)

pico_add_extra_outputs(${PROJECT_NAME})

# ---------------------------------------------------------------------------
# Host tool: sign_firmware – prepends a 16-byte validation header to the
# firmware binary.  Built with the host compiler (g++), not the Pico
# cross-compiler.
# ---------------------------------------------------------------------------
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/sign_firmware
    COMMAND g++ -O2 -std=c++17
            -I${CMAKE_CURRENT_SOURCE_DIR}/inc
            -o ${CMAKE_CURRENT_BINARY_DIR}/sign_firmware
            ${CMAKE_CURRENT_SOURCE_DIR}/src/sign_firmware.cpp
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/sign_firmware.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/inc/firmware_common.h
    COMMENT "Building sign_firmware (host tool)"
)

add_custom_target(sign_firmware_tool ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/sign_firmware
)

add_dependencies(${PROJECT_NAME} makefsdata sign_firmware_tool)

# disable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Optional: Print executable size as part of the post build process
add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND arm-none-eabi-size ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/sign_firmware
                ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.bin
                ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-${FIRMWARE_VERSION}-signed.bin
        COMMENT "Generating signed firmware binary"
)
