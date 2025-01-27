cmake_minimum_required(VERSION 3.20)

project(htmlgen C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_executable(makefsdata
        makefsdata.c
)

        

target_include_directories(makefsdata PRIVATE        
        ${CMAKE_CURRENT_LIST_DIR}/makefsdata
        ../
        ../../inc
        ../../pico-sdk/lib/lwip/src/include
        ../../pico-sdk/lib/lwip/contrib/ports/unix/port/include        
)

