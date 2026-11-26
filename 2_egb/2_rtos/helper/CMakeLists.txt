cmake_minimum_required(VERSION 3.12)
project(helper)

# Crear la biblioteca estática "helper" con los archivos fuente
add_library(helper STATIC
    src/helper.c
)

# Linkeo dependencias de la bibliotecas
target_link_libraries(helper
    pico_stdlib
    hardware_pwm
    hardware_clocks
)

# Incluir las cabeceras de la biblioteca
target_include_directories(helper PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)
