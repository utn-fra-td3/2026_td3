#ifndef APP_COMMON_H
#define APP_COMMON_H

// --- Includes ---
#include <stdint.h>
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
#define QUEUE_UART_TX_LEN            8

// --- Enums ---
typedef enum {
    SWEEP_PARAM_FREC_INICIO,
    SWEEP_PARAM_FREC_FINAL,
    SWEEP_PARAM_PUNTOS,
    SWEEP_PARAM_TIEMPO
} sweep_param_e;

typedef enum {
    MENU_EVT_CONFIG_SET // valor de configuracion propuesto desde lcd_display/uart, a validar por task_menu_config
} menu_evt_e;

typedef enum {
    DISPLAY_MSG_CONFIG_VALUE // valor de configuracion ya validado por task_menu_config, listo para mostrar
} display_msg_type_e;

typedef enum {
    UART_TX_CONFIG_ERROR,  // menu_config: valor de config fuera de rango (param + value)
    UART_TX_CONFIG_ACK,    // menu_config: valor de config aceptado (param + value)
    UART_TX_SWEEP_POINT    // sweep: punto medido del barrido (freq_hz + db)
} uart_tx_type_e;

// --- Tipos de mensajes ---
typedef struct {
    menu_evt_e    type;
    sweep_param_e param;
    uint32_t      value; // unidad base: Hz, puntos o segundos segun param
} menu_event_msg_t;

typedef struct {
    display_msg_type_e type;
    sweep_param_e       param;
    uint32_t            value; // unidad base: Hz, puntos o segundos segun param
} display_msg_t;

typedef struct {
    uart_tx_type_e type;
    sweep_param_e   param;    // valido para UART_TX_CONFIG_*
    uint32_t        value;    // valido para UART_TX_CONFIG_*
    uint32_t        freq_hz;  // valido para UART_TX_SWEEP_POINT
    float           db;       // valido para UART_TX_SWEEP_POINT
} uart_tx_msg_t; // task_uart es quien formatea el texto a partir de estos datos

// --- Handles compartidos (extern) ---
extern QueueHandle_t     queue_menu_events;
extern QueueHandle_t     queue_sweep_cmd;
extern QueueHandle_t     queue_display;
extern QueueHandle_t     queue_nvs_cmd;
extern QueueHandle_t     queue_uart_tx;
extern SemaphoreHandle_t sem_btn_press;

#endif // APP_COMMON_H
