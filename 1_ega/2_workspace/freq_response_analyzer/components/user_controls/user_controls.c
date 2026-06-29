// --- Includes ---
#include "user_controls.h"
#include "driver/gpio.h"
#include "esp_log.h"

// --- Defines privados ---
#define USER_CONTROLS_BTN1 GPIO_NUM_7
#define USER_CONTROLS_BTN2 GPIO_NUM_8
#define USER_CONTROLS_DEBOUNCE_MS 50

// --- Variables privadas ---
static const char *TAG = "user_controls";
static QueueHandle_t queue_btn_isr;

// --- Prototipos privados ---
static void inicializar_gpio(void);
static void gpio_btn1_handler(void *arg);
static void gpio_btn2_handler(void *arg);
static void atender_boton(gpio_num_t pin, menu_evt_e evento);

// --- Funciones ---

void task_user_controls(void *pvParameters)
{
    queue_btn_isr = xQueueCreate(4, sizeof(uint32_t));

    inicializar_gpio();

    while (1)
    {
        uint32_t gpio_num;
        xQueueReceive(queue_btn_isr, &gpio_num, portMAX_DELAY);

        if (gpio_num == USER_CONTROLS_BTN1)
        {
            atender_boton(USER_CONTROLS_BTN1, MENU_EVT_BTN1);
        }
        else
        {
            atender_boton(USER_CONTROLS_BTN2, MENU_EVT_BTN2);
        }
    }
}

static void inicializar_gpio(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1 << USER_CONTROLS_BTN1) | (1 << USER_CONTROLS_BTN2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1));
    ESP_ERROR_CHECK(gpio_isr_handler_add(USER_CONTROLS_BTN1, gpio_btn1_handler, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(USER_CONTROLS_BTN2, gpio_btn2_handler, NULL));
    ESP_LOGI(TAG, "GPIO inicializados: BTN1=GPIO%d, BTN2=GPIO%d", USER_CONTROLS_BTN1, USER_CONTROLS_BTN2);
}

static void gpio_btn1_handler(void *arg)
{
    gpio_intr_disable(USER_CONTROLS_BTN1);

    uint32_t gpio_num = USER_CONTROLS_BTN1;
    BaseType_t woken;
    xQueueSendFromISR(queue_btn_isr, &gpio_num, &woken);
}

static void gpio_btn2_handler(void *arg)
{
    gpio_intr_disable(USER_CONTROLS_BTN2);

    uint32_t gpio_num = USER_CONTROLS_BTN2;
    BaseType_t woken;
    xQueueSendFromISR(queue_btn_isr, &gpio_num, &woken);
}

static void atender_boton(gpio_num_t pin, menu_evt_e evento)
{
    vTaskDelay(pdMS_TO_TICKS(USER_CONTROLS_DEBOUNCE_MS));

    if (gpio_get_level(pin) == 0)
    {
        menu_event_msg_t ev = {.type = evento};
        xQueueSend(queue_menu_events, &ev, portMAX_DELAY);
        ESP_LOGI(TAG, "Boton GPIO%d pulsado", pin);
    }
    gpio_intr_enable(pin);
}