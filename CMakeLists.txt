cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD pico_w)

# Include build functions from Pico SDK

set(PICO_SDK_PATH ./pico-sdk)
set(FREERTOS_KERNEL_PATH ../../FreeRTOS-Kernel)

include(${CMAKE_CURRENT_SOURCE_DIR}/pico-sdk/pico_sdk_init.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FreeRTOS_Kernel_import.cmake)

set(SKIP_PICO_MALLOC 1)

pico_sdk_init()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

project(PicoHTTPServer C CXX ASM)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-volatile")

#list(JOIN CMAKE_CXX_FLAGS " " CMAKE_CXX_FLAGS)

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

add_custom_target(${name}-fs DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${name}.o)

add_custom_target(makefsdata
  COMMAND ../httpserver/makefsdata/build/makefsdata ${CMAKE_CURRENT_SOURCE_DIR}/httpserver/fs -f:${CMAKE_CURRENT_SOURCE_DIR}/httpserver/fsdata.c
  COMMENT "Running makefsdata"
)

add_executable(${PROJECT_NAME}
        main.cpp
        dhcpserver/dhcpserver.cpp
        dns/dnsserver.cpp
        httpserver/httpd.cpp
        httpserver/fs.cpp
        src/c_cpp_config.cpp
        src/settings.cpp
        src/tcp_server.cpp
        src/tcp_server_command.cpp
        src/hooks.cpp
        src/ssi.cpp
        src/post.cpp
        hal/src/serial.cpp
        src/sensor.cpp
        pico-sdk/lib/lwip/src/apps/mqtt/mqtt.c
        src/mqtt.cpp
        )

target_compile_definitions(${PROJECT_NAME} PRIVATE
        WIFI_SSID="PICONET"
        WIFI_PASSWORD="picopico"
        NO_SYS=0
        ETL_NO_STL
        ETL_NO_INITIALIZER_LIST
        ARDUINOJSON_ENABLE_STD_STREAM=0
        PICO_CXX_DISABLE_ALLOCATION_OVERRIDES=1
        #LWIP_DEBUG=1
        )
        
target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}
        inc
        hal/inc
        httpserver
        ${etl_SOURCE_DIR}/include
        )

target_link_libraries(${PROJECT_NAME}        
        pico_cyw43_arch_lwip_sys_freertos
        pico_stdlib
        hardware_uart
        hardware_flash
        FreeRTOS-Kernel-Heap4
        ArduinoJson
        etl::etl        
        )

if (TARGET Profiler)
    target_link_libraries(${PROJECT_NAME} 
            Profiler)
endif()

pico_add_extra_outputs(${PROJECT_NAME})

add_dependencies(${PROJECT_NAME} makefsdata)

# disable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Optional: Print executable size as part of the post build process
add_custom_command(TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND arm-none-eabi-size ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.elf
)