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

#define TASK_UART_STACK 3072
#define TASK_UART_PRIORITY 2

#define TASK_NVS_STACK 3072
#define TASK_NVS_PRIORITY 1

#define TASK_LVGL_PRIORITY 2

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
    SWEEP_PARAM_TIEMPO,
    SWEEP_PARAM_ALL // toda la configuracion, para "get config"
} sweep_param_e;

typedef enum
{
    EVENT_ORIGIN_LOCAL, // botones, touch o evento interno
    EVENT_ORIGIN_UART   // comando recibido por UART
} event_origin_e;

typedef enum
{
    CONFIG_OK,
    CONFIG_ERR_FSTART_RANGE,     // f_start fuera de rango
    CONFIG_ERR_FSTOP_RANGE,      // f_stop fuera de rango
    CONFIG_ERR_FRANGE,           // f_start no es menor que f_stop
    CONFIG_ERR_POINTS_RANGE,     // n_points fuera de rango
    CONFIG_ERR_SETTLE_TIME_LOW,  // tiempo de asentamiento insuficiente para la frecuencia inicial
    CONFIG_ERR_SETTLE_TIME_RANGE // tiempo de asentamiento fuera de rango
} config_result_e;

typedef enum
{
    UART_STATUS_OK,             // comando aceptado
    UART_STATUS_UNKNOWN_CMD,    // comando desconocido
    UART_STATUS_SYNTAX,         // parametro desconocido, falta valor, no numerico, linea larga
    UART_STATUS_OUT_OF_RANGE,   // valor fuera de rango
    UART_STATUS_INVALID_CONFIG, // validacion cruzada fallida al start
    UART_STATUS_INVALID_STATE   // comando de control que no aplica al estado actual
} uart_status_e;

typedef enum
{
    MENU_EVT_CONFIG_SET,     // cambio de un parametro (set)
    MENU_EVT_CONFIG_GET,     // consulta de un parametro o de toda la config (get)
    MENU_EVT_CONFIG_LOADED,  // configuracion completa leida de NVS
    MENU_EVT_START,          // iniciar barrido
    MENU_EVT_PAUSE,          // pausar barrido
    MENU_EVT_RESUME,         // reanudar barrido pausado
    MENU_EVT_BTN_PAUSE,      // boton de pausa de la pantalla, alterna pausar/reanudar segun el estado
    MENU_EVT_CANCEL,       // cancelar barrido/volver a configurar
    MENU_EVT_BTN1,           // boton fisico 1
    MENU_EVT_BTN2,           // boton fisico 2
    MENU_EVT_SWEEP_FINISHED
} menu_evt_e;

typedef enum
{
    DISPLAY_MSG_CONFIG_VALUE,  // valor de configuracion ya validado por task_menu_config, listo para mostrar
    DISPLAY_MSG_CONFIG_ERROR,  // valor o configuracion de conjunto invalida, mostrar popup con el motivo
    DISPLAY_MSG_SHOW_SWEEP,    // preparar el grafico con la config y mostrar pantalla de barrido
    DISPLAY_MSG_SWEEP_POINT,   // punto medido del barrido, task_sweep
    DISPLAY_MSG_SHOW_CONFIG,   // mostrar pantalla de configuracion
    DISPLAY_MSG_SHOW_PAUSE,    // mostrar pantalla de pausa
    DISPLAY_MSG_SHOW_RESUME,   // mostrar pantalla de reanudacion
    DISPLAY_MSG_SHOW_CANCEL    // mostrar pantalla de cancelacion
} display_msg_type_e;

typedef enum
{
    UART_TX_POINT,  // punto medido del barrido
    UART_TX_CONFIG, // respuesta a get
    UART_TX_STATUS, // estado de un comando: OK o ERR <n>
    UART_TX_SWEEP_START // encabezado de un barrido nuevo, antes del primer punto
} uart_tx_type_e;

typedef enum
{
    SWEEP_CMD_START,
    SWEEP_CMD_CANCEL,
    SWEEP_CMD_PAUSE,
    SWEEP_CMD_RESUME
} sweep_cmd_e;

typedef enum
{
    NVS_CMD_SAVE,
    NVS_CMD_LOAD
} nvs_cmd_e;

// --- Tipos de mensajes ---
typedef struct
{
    uint32_t frec_inicio;
    uint32_t frec_final;
    uint32_t puntos;
    uint32_t tiempo; // tiempo de asentamiento por punto, ms
} sweep_config_t;

typedef struct
{
    uint32_t index;   // indice del punto en el barrido, 0 a puntos-1
    uint32_t freq_hz; // frecuencia del punto medido, Hz
    float db;         // transferencia calculada en dB
} sweep_point_t;

typedef struct
{
    menu_evt_e type;
    event_origin_e origin;
    union
    {
        struct
        {
            sweep_param_e param;
            uint32_t value;
        } set;                  // MENU_EVT_CONFIG_SET
        sweep_param_e get_param; // MENU_EVT_CONFIG_GET
        sweep_config_t config;   // MENU_EVT_CONFIG_LOADED
    };
} menu_event_msg_t;

typedef struct
{
    display_msg_type_e type;
    union
    {
        struct
        {
            sweep_param_e param;
            uint32_t value;
        } value;               // DISPLAY_MSG_CONFIG_VALUE
        config_result_e error; // DISPLAY_MSG_CONFIG_ERROR
        sweep_config_t config; // DISPLAY_MSG_SHOW_SWEEP
        sweep_point_t point;   // DISPLAY_MSG_SWEEP_POINT
    };
} display_msg_t;

typedef struct
{
    uart_tx_type_e type;
    union
    {
        sweep_point_t point; // UART_TX_POINT
        struct
        {
            sweep_param_e param;
            sweep_config_t config;
        } get;            // UART_TX_CONFIG
        uart_status_e status; // UART_TX_STATUS
        sweep_config_t config; // UART_TX_SWEEP_START
    };
} uart_tx_msg_t;

typedef struct
{
    sweep_cmd_e cmd;
    sweep_config_t config;
} sweep_cmd_msg_t;

typedef struct
{
    nvs_cmd_e cmd;
    sweep_config_t config;
} nvs_cmd_msg_t;

// --- Handles compartidos (extern) ---
extern QueueHandle_t queue_menu_events;
extern QueueHandle_t queue_sweep_cmd;
extern QueueHandle_t queue_display;
extern QueueHandle_t queue_nvs_cmd;
extern QueueHandle_t queue_uart_tx;

#endif // APP_COMMON_H
