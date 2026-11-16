cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/lib/pico-sdk")
set(PICO_BOARD pico_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(toyless C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

add_subdirectory(lib/libxsm3)

add_executable(toyless
    src/main.c
    src/core/icc.c
    src/core/core1_main.c
    src/core/log.c
    src/auth/xsm3_service.c
    src/usb/usb_descriptors.c
    src/usb/usb_transport.c
    src/protocol/infinity_prng.c
)

target_include_directories(toyless PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/usb
    ${CMAKE_CURRENT_LIST_DIR}/src/auth
    ${CMAKE_CURRENT_LIST_DIR}/src/protocol
)

target_link_libraries(toyless
    pico_stdlib
    pico_multicore
    pico_util
    pico_unique_id
    pico_rand
    libxsm3
    tinyusb_device
    pico_cyw43_arch_none
)

pico_enable_stdio_uart(toyless 1)
pico_enable_stdio_usb(toyless 0)

pico_add_extra_outputs(toyless)
