// --- Includes ---
#include "sweep.h"
#include "ad9833.h"
#include "adc.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <math.h>

// --- Defines privados ---
#define SWEEP_RST_VIN GPIO_NUM_6
#define SWEEP_RST_VO GPIO_NUM_16
#define SWEEP_RESET_PULSO_MS 10
#define SWEEP_MUESTRAS 10 // cantidad de lecturas de ADC por frecuencia
#define SWEEP_VIN_MIN_MV 1 // minimo valor aceptable del ADC, evita log10(0) con vin = 0

// --- Variables privadas ---
static const char *TAG = "sweep";

// --- Prototipos privados ---
static void inicializar_gpio(void);
static void resetear_detectores_pico(void);
static uint32_t calcular_frecuencia(uint32_t frec_inicio, uint32_t frec_final, uint32_t puntos, uint32_t i);
static float medir_punto(uint32_t frec_hz, uint32_t tiempo_asentamiento_ms);
static void ejecutar_barrido(const sweep_config_t *config);

// --- Funciones ---

void task_sweep(void *pvParameters)
{
    sweep_cmd_msg_t cmd;

    inicializar_gpio();
    ad9833_init();
    adc_init();

    while (1)
    {
        if (xQueueReceive(queue_sweep_cmd, &cmd, portMAX_DELAY) == pdTRUE)
        {
            if (cmd.cmd == SWEEP_CMD_START)
            {
                ejecutar_barrido(&cmd.config);
            }
        }
    }
}

static void inicializar_gpio(void)
{
    gpio_set_direction(SWEEP_RST_VIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SWEEP_RST_VO, GPIO_MODE_OUTPUT);
    gpio_set_level(SWEEP_RST_VIN, 0);
    gpio_set_level(SWEEP_RST_VO, 0);
}

static void resetear_detectores_pico(void)
{
    gpio_set_level(SWEEP_RST_VIN, 1);
    gpio_set_level(SWEEP_RST_VO, 1);
    vTaskDelay(pdMS_TO_TICKS(SWEEP_RESET_PULSO_MS));
    gpio_set_level(SWEEP_RST_VIN, 0);
    gpio_set_level(SWEEP_RST_VO, 0);
}

// paso logaritmico
static uint32_t calcular_frecuencia(uint32_t frec_inicio, uint32_t frec_final, uint32_t puntos, uint32_t i)
{
    if (puntos <= 1)
    {
        return frec_inicio;
    }
    if (i >= puntos - 1)
    {
        return frec_final;
    }

    float exponente = (float)i / (float)(puntos - 1);
    float frec = frec_inicio * powf((float)frec_final / (float)frec_inicio, exponente);
    return (uint32_t)(frec + 0.5f);
}

static float medir_punto(uint32_t frec_hz, uint32_t tiempo_asentamiento_ms)
{
    ad9833_set_freq(frec_hz);
    ad9833_disable_output(); // apagar DDS antes del reset del detector de pico
    resetear_detectores_pico();
    ad9833_enable_output(); // encender DDS y espero tiemo de asentamiento
    vTaskDelay(pdMS_TO_TICKS(tiempo_asentamiento_ms));

    int suma_vin = 0;
    int suma_vout = 0;
    for (int i = 0; i < SWEEP_MUESTRAS; i++)
    {
        int vin = adc_read_vin_mv(); // pico de la respuesta del DUT
        if (vin < SWEEP_VIN_MIN_MV)
        {
            vin = SWEEP_VIN_MIN_MV; // por debajo de esto el ADC no puede distinguir la señal del piso de ruido
        }
        suma_vin += vin;
        suma_vout += adc_read_vout_mv(); // pico de la excitacion del DUT
    }
    float vin_prom = (float)suma_vin / SWEEP_MUESTRAS;
    float vout_prom = (float)suma_vout / SWEEP_MUESTRAS;

    //ESP_LOGI(TAG, "frec.: %lu Hz, vin_prom = %.2f mV, vout_prom = %.2f mV", frec_hz, vin_prom, vout_prom);

    return 20.0f * log10f(vin_prom / vout_prom); // transferencia = respuesta/excitacion = vin/vout
}

static void ejecutar_barrido(const sweep_config_t *config)
{
    ESP_LOGI(TAG, "iniciando barrido: %lu Hz a %lu Hz, %lu puntos, asentamiento %lu ms", config->frec_inicio, config->frec_final, config->puntos, config->tiempo);

    uint32_t frec_anterior = 0;
    float db_anterior = 0.0f;
    for (uint32_t i = 0; i < config->puntos; i++)
    {
        uint32_t frec_hz = calcular_frecuencia(config->frec_inicio, config->frec_final, config->puntos, i);
        float db;

        if (frec_hz == frec_anterior)
        {
            db = db_anterior; // Se mantiene el valor anterior
        }
        else
        {
            frec_anterior = frec_hz;
            db = medir_punto(frec_hz, config->tiempo);
        }
        db_anterior = db;

        display_msg_t msg_disp = {
            .type = DISPLAY_MSG_SWEEP_POINT,
            .freq_hz = frec_hz,
            .db = db,
        };
        if (xQueueSend(queue_display, &msg_disp, 0) != pdTRUE)
        {
            ESP_LOGW(TAG, "queue_display llena, punto no mostrado");
        }

        uart_tx_msg_t msg_uart = {
            .freq_hz = frec_hz,
            .db = db,
        };
        if (xQueueSend(queue_uart_tx, &msg_uart, 0) != pdTRUE)
        {
            ESP_LOGW(TAG, "queue_uart_tx llena, punto no enviado");
        }
    }

    ad9833_disable_output();
    ESP_LOGI(TAG, "barrido finalizado");
}
