cmake_minimum_required(VERSION 3.12)

# Conditionally set Pico core (RP2350_ARM_NTZ for pico2 / RP2040 for pico)
if(${PICO_BOARD} STREQUAL "pico2" OR ${PICO_BOARD} STREQUAL "pico2_w")
    set(CORE RP2350_ARM_NTZ)
elseif(${PICO_BOARD} STREQUAL "pico" OR ${PICO_BOARD} STREQUAL "pico_w")
    set(CORE RP2040)
endif()

# Add FreeRTOS source files
add_library(freertos STATIC
    event_groups.c
    list.c
    queue.c
    stream_buffer.c
    tasks.c
    timers.c
    portable/GCC/${CORE}/port.c
)

# Chack if dynamic allocation is supported
if(${FREERTOS_SUPPORT_DYNAMIC_ALLOCATION} STREQUAL "ON")
    # Default heap implementation
    if(NOT DEFINED FREERTOS_HEAP)
        set(FREERTOS_HEAP "heap_4")
    endif()
    message(STATUS "FreeRTOS: Usando ${FREERTOS_HEAP}")
    target_sources(freertos PRIVATE portable/MemMang/${FREERTOS_HEAP}.c)
else()
    message(STATUS "FreeRTOS: Memoria dinámica deshabilitada")
endif()

# Add extra source files when core is RP2350
if(${CORE} STREQUAL "RP2350_ARM_NTZ")
target_sources(freertos PRIVATE
    portable/GCC/${CORE}/mpu_wrappers_v2_asm.c
    portable/GCC/${CORE}/portasm.c
)
endif()

# Extra User Config?
if(DEFINED FREERTOS_USER_CONFIG AND EXISTS "${FREERTOS_USER_CONFIG}/FreeRTOSConfig_User.h")
    message(STATUS "FreeRTOS: Agreando configuración de usuario desde ${FREERTOS_USER_CONFIG}/FreeRTOSConfig_User.h")
    target_compile_definitions(freertos PUBLIC FREERTOS_USER_CONFIG)
    target_include_directories(freertos PUBLIC ${FREERTOS_USER_CONFIG})
endif()

# Include FreeRTOS header files
target_include_directories(freertos PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/portable/GCC/${CORE}/include
)

# Add dependencies
target_link_libraries(freertos PUBLIC pico_stdlib hardware_exception)