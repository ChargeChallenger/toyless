cmake_minimum_required(VERSION 3.13)

add_library(libxsm3
    excrypt_des.c
    excrypt_parve.c
    excrypt_sha.c
    usbdsec.c
    xsm3.c
)

target_include_directories(libxsm3 PUBLIC ${CMAKE_CURRENT_LIST_DIR})

target_compile_options(libxsm3 PRIVATE -Os)

target_link_libraries(libxsm3
    pico_stdlib
    pico_rand
)
