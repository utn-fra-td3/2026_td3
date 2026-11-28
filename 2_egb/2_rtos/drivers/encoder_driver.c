#include "encoder_driver.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define ENCODER_CLK_IO  32
#define ENCODER_DT_IO   33
#define ENCODER_SW_IO   25

// ── DEFINICIONES COMPARTIDAS CON MAIN ──
typedef enum {
    CMD_ENC_UP,       // Giro hacia arriba
    CMD_ENC_DOWN,     // Giro hacia abajo
    CMD_ENC_BTN,      // Botón propio del encoder
    CMD_UART_FREQ,    // UART frecuencia
    CMD_UART_WAVE     // UART forma de onda
} unified_cmd_t;

typedef struct {
    unified_cmd_t cmd;
    int value;
} unified_msg_t;

extern QueueHandle_t control_queue;

// Variable interna para contar transiciones
static volatile int32_t raw_count = 0;

// ISR Inmune a rebotes + FreeRTOS Queues
static void IRAM_ATTR encoder_isr_handler(void *arg) {
    uint32_t pin = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    unified_msg_t event;
    event.value = 0; // El encoder no utiliza valor numérico

    // ── 1. SI LA INTERRUPCIÓN VINO DEL BOTÓN ──
    if (pin == ENCODER_SW_IO) {
        event.cmd = CMD_ENC_BTN;
        xQueueSendFromISR(control_queue, &event, &xHigherPriorityTaskWoken);
    } 
    // ── 2. SI LA INTERRUPCIÓN VINO DE LAS PERILLAS ──
    else {
        static const int8_t state_table[] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
        static uint8_t state = 0;

        uint8_t clk = gpio_get_level(ENCODER_CLK_IO);
        uint8_t dt = gpio_get_level(ENCODER_DT_IO);

        state = (state << 2) | (dt << 1) | clk;
        raw_count += state_table[state & 0x0F];

        // ¿Juntamos 4 transiciones (1 click físico hacia arriba)?
        if (raw_count >= 4) {
            event.cmd = CMD_ENC_UP;
            raw_count -= 4; // Reseteamos la cuenta de este click
            xQueueSendFromISR(control_queue, &event, &xHigherPriorityTaskWoken);
        } 
        // ¿Juntamos 4 transiciones (1 click físico hacia abajo)?
        else if (raw_count <= -4) {
            event.cmd = CMD_ENC_DOWN;
            raw_count += 4; // Reseteamos la cuenta de este click
            xQueueSendFromISR(control_queue, &event, &xHigherPriorityTaskWoken);
        }
    }

    // Forzamos el cambio de contexto si la tarea que espera la cola es de mayor prioridad
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

void encoder_init(void) {
    // Configuramos CLK y DT para interrumpir en CUALQUIER flanco
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENCODER_CLK_IO) | (1ULL << ENCODER_DT_IO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE, 
    };
    gpio_config(&io_conf);

    // Configuramos el BOTÓN para interrumpir SOLO al presionar (flanco de bajada)
    gpio_config_t sw_conf = {
        .pin_bit_mask = (1ULL << ENCODER_SW_IO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&sw_conf);

    // Instalamos el servicio global si no estaba instalado
    gpio_install_isr_service(0);
    
    // Anclamos la interrupción pasando el número de pin como argumento
    gpio_isr_handler_add(ENCODER_CLK_IO, encoder_isr_handler, (void*)ENCODER_CLK_IO);
    gpio_isr_handler_add(ENCODER_DT_IO,  encoder_isr_handler, (void*)ENCODER_DT_IO);
    gpio_isr_handler_add(ENCODER_SW_IO,  encoder_isr_handler, (void*)ENCODER_SW_IO);
}