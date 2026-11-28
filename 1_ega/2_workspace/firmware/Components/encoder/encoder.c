#include "encoder.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define ENCODER_CLK 4  
#define ENCODER_DT  5  
#define ENCODER_SW  6  

// Tiempos para evitar el ruido eléctrico del Encoder
#define DEBOUNCE_TIME_CLK_US 50000   
#define DEBOUNCE_TIME_SW_US  300000 

static QueueHandle_t q_eventos_local;
static volatile int64_t last_time_clk = 0;
static volatile int64_t last_time_sw = 0;

static void IRAM_ATTR encoder_isr_handler(void* arg) {
    uint32_t pin = (uint32_t) arg;
    int64_t current_time = esp_timer_get_time();
    encoder_event_t event;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (pin == ENCODER_CLK) {
        if ((current_time - last_time_clk) > DEBOUNCE_TIME_CLK_US) {
            last_time_clk = current_time;
            if (gpio_get_level(ENCODER_DT) == 1) {
                event = ENCODER_UP;
            } else {
                event = ENCODER_DOWN;
            }
            xQueueSendFromISR(q_eventos_local, &event, &xHigherPriorityTaskWoken);
        }
    } 
    else if (pin == ENCODER_SW) {
        if (gpio_get_level(ENCODER_SW) == 0 && (current_time - last_time_sw) > DEBOUNCE_TIME_SW_US) {
            last_time_sw = current_time;
            event = ENCODER_CLICK;
            xQueueSendFromISR(q_eventos_local, &event, &xHigherPriorityTaskWoken);
        }
    }

    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void encoder_init(QueueHandle_t cola_eventos) {
    q_eventos_local = cola_eventos;

    gpio_config_t io_conf = {};
    
    // CLK: Solo interrumpe al bajar
    io_conf.intr_type = GPIO_INTR_NEGEDGE; 
    io_conf.mode = GPIO_MODE_INPUT;
    // Se configura como apagado el pull-up interno, para que actuen las resistencias físicas del módulo
    io_conf.pull_up_en = 0; 
    io_conf.pin_bit_mask = (1ULL<<ENCODER_CLK);
    gpio_config(&io_conf);

    // DT
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.pull_up_en = 0; 
    io_conf.pin_bit_mask = (1ULL<<ENCODER_DT);
    gpio_config(&io_conf);

    // SW: Clic del botón
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pull_up_en = 1; 
    io_conf.pin_bit_mask = (1ULL<<ENCODER_SW);
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(ENCODER_CLK, encoder_isr_handler, (void*) ENCODER_CLK);
    gpio_isr_handler_add(ENCODER_SW,  encoder_isr_handler, (void*) ENCODER_SW);
}