// --- Includes ---
#include "user_controls.h"
#include "driver/gpio.h"
#include "esp_log.h"

// --- Defines privados ---
#define USER_CONTROLS_BTN1 GPIO_NUM_7
#define USER_CONTROLS_BTN2 GPIO_NUM_8
#define USER_CONTROLS_DEBOUNCE_MS 50

// --- Definicion de handlers de interrupcion ---
static void gpio_btn1_handler(void *arg);
static void gpio_btn2_handler(void *arg);

// --- Creacion de semaforos ---
static SemaphoreHandle_t sem_btn1;
static SemaphoreHandle_t sem_btn2;

// --- Definicion de tareas ---

static void task_btn1(void *pvParameters);
static void task_btn2(void *pvParameters);
static void atender_boton1(gpio_num_t pin, menu_evt_e evento);
static void atender_boton2(gpio_num_t pin, menu_evt_e evento);
// --- Prototipos privados ---
static void inicializar_gpio(void);


// --- Funciones ---

void task_user_controls(void *pvParameters)
{
  
    sem_btn1 = xSemaphoreCreateBinary();
    sem_btn2 = xSemaphoreCreateBinary();
    configASSERT(sem_btn1 != NULL && sem_btn2 != NULL);

    xTaskCreate(task_btn1, "task_btn1", 3072, NULL, 5, NULL);
    xTaskCreate(task_btn2, "task_btn2", 3072, NULL, 5, NULL);

    inicializar_gpio();

            
    vTaskDelete(NULL); 
       
}


static void IRAM_ATTR gpio_btn1_handler(void *arg)
{
    BaseType_t woken = pdFALSE;
    gpio_intr_disable(USER_CONTROLS_BTN1);
    xSemaphoreGiveFromISR(sem_btn1, &woken);
    portYIELD_FROM_ISR(woken);
}


static void IRAM_ATTR gpio_btn2_handler(void *arg)
{
    BaseType_t woken = pdFALSE;
    gpio_intr_disable(USER_CONTROLS_BTN2);
    xSemaphoreGiveFromISR(sem_btn2, &woken);
    portYIELD_FROM_ISR(woken);
}


static void inicializar_gpio(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << USER_CONTROLS_BTN1) | (1ULL << USER_CONTROLS_BTN2),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1));
    ESP_ERROR_CHECK(gpio_isr_handler_add(USER_CONTROLS_BTN1, gpio_btn1_handler, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(USER_CONTROLS_BTN2, gpio_btn2_handler, NULL));
}

static void task_btn1(void *pvParameters)
{
    while (1)
    {
        xSemaphoreTake(sem_btn1, portMAX_DELAY);
        atender_boton1(USER_CONTROLS_BTN1, MENU_EVT_BTN1);
    }
}

static void task_btn2(void *pvParameters)
{
    while (1)
    {
        xSemaphoreTake(sem_btn2, portMAX_DELAY);
        atender_boton2(USER_CONTROLS_BTN2, MENU_EVT_BTN2);
    }
}

static void atender_boton1(gpio_num_t pin, menu_evt_e evento)
{
    vTaskDelay(pdMS_TO_TICKS(USER_CONTROLS_DEBOUNCE_MS));

    if (gpio_get_level(pin) == 0)
    {
        menu_event_msg_t ev = { .type = evento, .origin = EVENT_ORIGIN_LOCAL };
        xQueueSend(queue_menu_events, &ev, portMAX_DELAY);

        while (gpio_get_level(pin) == 0)
            vTaskDelay(pdMS_TO_TICKS(USER_CONTROLS_DEBOUNCE_MS));
    }

   
    gpio_intr_enable(pin);
}

static void atender_boton2(gpio_num_t pin, menu_evt_e evento)
{
    vTaskDelay(pdMS_TO_TICKS(USER_CONTROLS_DEBOUNCE_MS));

    if (gpio_get_level(pin) == 0)
    {
        menu_event_msg_t ev = { .type = evento, .origin = EVENT_ORIGIN_LOCAL };
        xQueueSend(queue_menu_events, &ev, portMAX_DELAY);

        while (gpio_get_level(pin) == 0)
            vTaskDelay(pdMS_TO_TICKS(USER_CONTROLS_DEBOUNCE_MS));
    }

  
    gpio_intr_enable(pin);
}