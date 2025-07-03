cmake_minimum_required(VERSION 3.12)

# Conditionally set Pico core (RP2350_ARM_NTZ for pico2 / RP2040 for pico)
if(${PICO_BOARD} STREQUAL "pico2" OR ${PICO_BOARD} STREQUAL "pico2_w")
    set(CORE RP2350_ARM_NTZ)
elseif(${PICO_BOARD} STREQUAL "pico" OR ${PICO_BOARD} STREQUAL "pico_w")
    set(CORE RP2040)
endif()

# Default heap implementation
if(NOT DEFINED FREERTOS_HEAP)
    set(FREERTOS_HEAP "heap_4")
endif()
message(STATUS "FreeRTOS: Usando ${FREERTOS_HEAP}")

# Add FreeRTOS source files
add_library(freertos STATIC
    event_groups.c
    list.c
    queue.c
    stream_buffer.c
    tasks.c
    timers.c
    portable/GCC/${CORE}/port.c
    portable/MemMang/${FREERTOS_HEAP}.c
)

# Add extra source files when core is RP2350
if(${CORE} STREQUAL "RP2350_ARM_NTZ")
target_sources(freertos PRIVATE
    portable/GCC/${CORE}/mpu_wrappers_v2_asm.c
    portable/GCC/${CORE}/portasm.c
)
endif()

# Library FreeRTOSConfig.h or application FreeRTOSConfig.h?
if(DEFINED APP_FREERTOS_CONFIG AND EXISTS "${APP_FREERTOS_CONFIG}/FreeRTOSConfig.h")
    message(STATUS "FreeRTOS: Usando un FreeRTOSConfig.h de aplicación desde ${APP_FREERTOS_CONFIG}")
    set(FREERTOS_CONFIG_DIR ${APP_FREERTOS_CONFIG})
else()
    message(STATUS "FreeRTOS: Usando default FreeRTOSConfig.h")
    set(FREERTOS_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Include FreeRTOS header files
target_include_directories(freertos PUBLIC
    ${FREERTOS_CONFIG_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/portable/GCC/${CORE}/include
)


# Add dependencies
target_link_libraries(freertos PUBLIC pico_stdlib hardware_exception)