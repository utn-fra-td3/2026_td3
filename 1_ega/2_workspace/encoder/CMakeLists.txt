add_library(encoder STATIC
    src/encoder.c
)

target_link_libraries(encoder
    pico_stdlib
    hardware_gpio
)

target_include_directories(encoder PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)
