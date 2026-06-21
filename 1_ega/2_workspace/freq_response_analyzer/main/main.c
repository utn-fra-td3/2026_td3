// --- Includes ---
#include "app_common.h"
#include "user_controls.h"
#include "menu_config.h"
#include "lcd_display.h"
#include "sweep.h"
#include "uart.h"
#include "nvs_store.h"

// --- Definicion de handles ---
QueueHandle_t queue_menu_events = NULL;
QueueHandle_t queue_sweep_cmd = NULL;
QueueHandle_t queue_display = NULL;
QueueHandle_t queue_nvs_cmd = NULL;
QueueHandle_t queue_uart_rx = NULL;
SemaphoreHandle_t mutex_uart_tx = NULL;
SemaphoreHandle_t sem_btn_press = NULL;

void app_main(void)
{

    // --- Creacion de recursos ---

    queue_menu_events = xQueueCreate(QUEUE_MENU_EVENTS_LEN, sizeof(menu_event_msg_t));
    configASSERT(queue_menu_events != NULL);
    // queue_sweep_cmd = xQueueCreate(QUEUE_SWEEP_CMD_LEN, sizeof(sweep_cmd_msg_t));
    queue_display = xQueueCreate(QUEUE_DISPLAY_LEN, sizeof(display_msg_t));
    configASSERT(queue_display != NULL);
    // queue_nvs_cmd = xQueueCreate(QUEUE_NVS_CMD_LEN, sizeof(nvs_cmd_msg_t));
    // queue_uart_rx = xQueueCreate(QUEUE_UART_RX_LEN, sizeof(uint8_t));
    // mutex_uart_tx = xSemaphoreCreateMutex();
    // sem_btn_press = xSemaphoreCreateBinary();

    // --- Arranque de tareas ---

    // xTaskCreate(task_user_controls, "task_user_controls", TASK_USER_CONTROLS_STACK, NULL, TASK_USER_CONTROLS_PRIORITY, NULL);
    xTaskCreate(task_menu_config, "task_menu_config", TASK_MENU_CONFIG_STACK, NULL, TASK_MENU_CONFIG_PRIORITY, NULL);
    xTaskCreate(task_lcd_display, "task_lcd_display", TASK_LCD_DISPLAY_STACK, NULL, TASK_LCD_DISPLAY_PRIORITY, NULL);
    // xTaskCreate(task_sweep, "task_sweep", TASK_SWEEP_STACK, NULL, TASK_SWEEP_PRIORITY, NULL);
    // xTaskCreate(task_uart, "task_uart", TASK_UART_STACK, NULL, TASK_UART_PRIORITY, NULL);
    // xTaskCreate(task_nvs, "task_nvs", TASK_NVS_STACK, NULL, TASK_NVS_PRIORITY, NULL);
}
