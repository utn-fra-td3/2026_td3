add_library(ads1115 STATIC
    src/ads1115.c
)

target_link_libraries(ads1115
    pico_stdlib
    hardware_i2c
)

target_include_directories(ads1115 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)
