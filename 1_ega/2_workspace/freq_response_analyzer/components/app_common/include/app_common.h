#ifndef APP_COMMON_H
#define APP_COMMON_H

// --- Includes ---
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// --- Parametros de tareas ---
#define TASK_USER_CONTROLS_STACK     2048
#define TASK_USER_CONTROLS_PRIORITY  2
#define TASK_MENU_CONFIG_STACK       2048
#define TASK_MENU_CONFIG_PRIORITY    3
#define TASK_LCD_DISPLAY_STACK       4096
#define TASK_LCD_DISPLAY_PRIORITY    2
#define TASK_SWEEP_STACK             4096
#define TASK_SWEEP_PRIORITY          2
#define TASK_UART_STACK              2048
#define TASK_UART_PRIORITY           2
#define TASK_NVS_STACK               2048
#define TASK_NVS_PRIORITY            1

// --- Longitud de colas ---
#define QUEUE_MENU_EVENTS_LEN        8
#define QUEUE_SWEEP_CMD_LEN          4
#define QUEUE_DISPLAY_LEN            8
#define QUEUE_NVS_CMD_LEN            4
#define QUEUE_UART_RX_LEN            16

// --- Enums ---

// --- Tipos de mensajes ---

// --- Handles compartidos (extern) ---
extern QueueHandle_t     queue_menu_events;
extern QueueHandle_t     queue_sweep_cmd;
extern QueueHandle_t     queue_display;
extern QueueHandle_t     queue_nvs_cmd;
extern QueueHandle_t     queue_uart_rx;
extern SemaphoreHandle_t mutex_uart_tx;
extern SemaphoreHandle_t sem_btn_press;

#endif // APP_COMMON_H
