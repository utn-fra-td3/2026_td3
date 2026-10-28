# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PICO_BOARD pico2_w CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(firmware C CXX ASM)

pico_sdk_init()

set(FREERTOS_SUPPORT_DYNAMIC_ALLOCATION ON CACHE BOOL "Enable FreeRTOS dynamic allocation")

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../freertos ${CMAKE_BINARY_DIR}/freertos)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../lcd ${CMAKE_BINARY_DIR}/lcd)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../encoder ${CMAKE_BINARY_DIR}/encoder)

add_executable(firmware firmware.c settings_storage.c)

pico_set_program_name(firmware "firmware")
pico_set_program_version(firmware "0.1")

pico_enable_stdio_uart(firmware 0)
pico_enable_stdio_usb(firmware 1)

# Keep both Pico SDK USB reset mechanisms enabled so VS Code's Run task can
# reboot a running board into BOOTSEL without pressing the physical button.
target_compile_definitions(firmware PRIVATE
    PICO_ENABLE_USB_RESET_VIA_BAUD_RATE=1
    PICO_ENABLE_USB_RESET_VIA_VENDOR_INTERFACE=1
)

target_link_libraries(firmware
    freertos
    lcd
    encoder
    pico_stdlib
    hardware_i2c
    hardware_adc
    hardware_pwm
    hardware_clocks
    hardware_uart
    hardware_flash
    pico_flash
)

target_include_directories(firmware PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(firmware)
