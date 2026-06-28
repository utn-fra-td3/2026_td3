#ifndef APP_COMMON_H
#define APP_COMMON_H

// --- Includes ---
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// --- Parametros de tareas ---
#define TASK_USER_CONTROLS_STACK 2048
#define TASK_USER_CONTROLS_PRIORITY 2

#define TASK_MENU_CONFIG_STACK 2048
#define TASK_MENU_CONFIG_PRIORITY 3

#define TASK_LCD_DISPLAY_STACK 4096
#define TASK_LCD_DISPLAY_PRIORITY 2

#define TASK_SWEEP_STACK 4096
#define TASK_SWEEP_PRIORITY 2

#define TASK_UART_STACK 2048
#define TASK_UART_PRIORITY 2

#define TASK_NVS_STACK 2048
#define TASK_NVS_PRIORITY 1

// --- Longitud de colas ---
#define QUEUE_MENU_EVENTS_LEN 8
#define QUEUE_SWEEP_CMD_LEN 4
#define QUEUE_DISPLAY_LEN 8
#define QUEUE_NVS_CMD_LEN 4
#define QUEUE_UART_TX_LEN 8

// --- Enums ---
typedef enum
{
    SWEEP_PARAM_FREC_INICIO,
    SWEEP_PARAM_FREC_FINAL,
    SWEEP_PARAM_PUNTOS,
    SWEEP_PARAM_TIEMPO
} sweep_param_e;

typedef enum
{
    MENU_EVT_CONFIG_SET,
    MENU_EVT_SWEEP_START
} menu_evt_e;

typedef enum
{
    DISPLAY_MSG_CONFIG_VALUE,      // valor de configuracion ya validado por task_menu_config, listo para mostrar
    DISPLAY_MSG_SWEEP_START_OK,    // configuracion de conjunto valida, pasar a la pantalla de barrido
    DISPLAY_MSG_SWEEP_START_ERROR, // configuracion de conjunto invalida, mostrar popup con el motivo
    DISPLAY_MSG_SWEEP_POINT        // punto medido del barrido (freq_hz + db), task_sweep
} display_msg_type_e;

typedef enum
{
    SWEEP_START_OK,
    SWEEP_START_ERR_FSTART_RANGE,  // f_start fuera de rango
    SWEEP_START_ERR_FSTOP_RANGE,   // f_stop fuera de rango
    SWEEP_START_ERR_FRANGE,        // f_start no es menor que f_stop
    SWEEP_START_ERR_POINTS_RANGE,  // n_points fuera de rango
    SWEEP_START_ERR_SETTLE_TIME_LOW // tiempo de asentamiento insuficiente para la frecuencia inicial
} sweep_start_result_e;

typedef enum
{
    SWEEP_CMD_START,
    SWEEP_CMD_CANCEL,
    SWEEP_CMD_PAUSE,
    SWEEP_CMD_RESUME
} sweep_cmd_e;

// --- Tipos de mensajes ---
typedef struct
{
    menu_evt_e type;
    sweep_param_e param;
    uint32_t value;
} menu_event_msg_t;

typedef struct
{
    display_msg_type_e type;
    sweep_param_e param;
    uint32_t value;
    sweep_start_result_e motivo;
    uint32_t freq_hz;
    float db;
    uint32_t frec_inicio;
    uint32_t frec_final;
    uint32_t puntos;
} display_msg_t;

typedef struct
{
    uint32_t frec_inicio;
    uint32_t frec_final;
    uint32_t puntos;
    uint32_t tiempo; // tiempo de asentamiento por punto, ms
} sweep_config_t;

typedef struct
{
    sweep_cmd_e cmd;
    sweep_config_t config;
} sweep_cmd_msg_t;

typedef struct
{
    uint32_t freq_hz; // frecuencia del punto medido, Hz
    float db;         // transferencia calculada en dB
} uart_tx_msg_t;

// --- Handles compartidos (extern) ---
extern QueueHandle_t queue_menu_events;
extern QueueHandle_t queue_sweep_cmd;
extern QueueHandle_t queue_display;
extern QueueHandle_t queue_nvs_cmd;
extern QueueHandle_t queue_uart_tx;
extern SemaphoreHandle_t sem_btn_press;

#endif // APP_COMMON_H
