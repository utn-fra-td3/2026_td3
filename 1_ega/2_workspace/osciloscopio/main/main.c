#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "ssd1306.h"
#include "esp_adc/adc_continuous.h"
#include "driver/ledc.h"
#include "driver/uart.h"

static const char *TAG = "OSCILOSCOPIO_FINAL";

// ====================================================================
// 1. CONFIGURACIONES DE HARDWARE Y PINES
// ====================================================================
// Encoder y Botón
#define EXAMPLE_PCNT_HIGH_LIMIT 100
#define EXAMPLE_PCNT_LOW_LIMIT  -100
#define EXAMPLE_EC11_GPIO_A 4
#define EXAMPLE_EC11_GPIO_B 6
#define BUTTON_GPIO 18

// ADC
#define EXAMPLE_ADC_UNIT             ADC_UNIT_1
#define EXAMPLE_ADC_CONV_MODE        ADC_CONV_SINGLE_UNIT_1
#define EXAMPLE_ADC_ATTEN            ADC_ATTEN_DB_12
#define EXAMPLE_ADC_BIT_WIDTH        SOC_ADC_DIGI_MAX_BITWIDTH
#define EXAMPLE_READ_LEN             4096 
#define NUM_MUESTRAS                 2048
#define PUNTOS_PANTALLA              400 

// PWM (Señal de Prueba)
#define PWM_OUTPUT_GPIO    5   
#define PWM_FREQ_HZ        50  
#define HISTERESIS         100

// ====================================================================
// 2. ESTRUCTURAS, TIPOS Y VARIABLES GLOBALES
// ====================================================================
typedef enum { ESTADO_MENU_PRINCIPAL, ESTADO_TIEMPO, ESTADO_AMPLITUD, ESTADO_TRIGGER } estado_menu_t;
typedef enum { FLANCO_ASCENDENTE = 0, FLANCO_DESCENDENTE = 1 } tipo_flanco_t;

// Estructura para el Mailbox de Configuración
typedef struct {
    tipo_flanco_t flanco;
    uint32_t nivel;
    uint32_t tiempo_ms;
    float amplitud_v;
} config_osciloscopio_t;

// Estructura de parámetros para el menú
typedef struct {
    SSD1306_t *display;
    pcnt_unit_handle_t pcnt_unit;
} menu_params_t;

// Estructura para empaquetar datos hacia la UART
typedef struct {
    uint16_t ventana[PUNTOS_PANTALLA];
    uint8_t flanco;
    uint32_t nivel;
    uint32_t tiempo;
    float amplitud;
} trama_uart_t;

// Colas del Sistema (Nombres sincronizados con el diagrama)
static QueueHandle_t button_queue = NULL;      
static QueueHandle_t full_queue = NULL;
static QueueHandle_t Uart_queue = NULL;
static QueueHandle_t config_queue = NULL; // Mailbox

// Manejadores de Tareas y Tiempos
static TaskHandle_t xAdcTaskHandle = NULL;
static TaskHandle_t xTriggerTaskHandle = NULL;
static TaskHandle_t xUartTaskHandle = NULL;
static TickType_t last_press = 0;

// Comunicación de UI
volatile bool boton_presionado = false;

// Buffers del ADC
static adc_channel_t channel[1] = {ADC_CHANNEL_6}; 
static uint8_t hardware_read_buffer[EXAMPLE_READ_LEN];
static adc_continuous_data_t parsed_data_buffer[EXAMPLE_READ_LEN / SOC_ADC_DIGI_RESULT_BYTES];
static uint16_t buffer_A[NUM_MUESTRAS];
static uint16_t buffer_B[NUM_MUESTRAS];

// ====================================================================
// 3. RUTINAS DE SERVICIO DE INTERRUPCIÓN (ISRs)
// ====================================================================
static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(button_queue, &gpio_num, NULL);
}

static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data) {
    BaseType_t mustYield = pdFALSE;
    vTaskNotifyGiveFromISR(xAdcTaskHandle, &mustYield);
    return (mustYield == pdTRUE);
}

// ====================================================================
// 4. TAREAS DE LA INTERFAZ DE USUARIO Y CONTROL
// ====================================================================
void vButtonTask(void *pvParameters) {
    uint32_t io_num;
    while (1) {
        if (xQueueReceive(button_queue, &io_num, portMAX_DELAY)) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_press) > pdMS_TO_TICKS(250)) {
                if (gpio_get_level(BUTTON_GPIO) == 0) {
                    boton_presionado = true; 
                    last_press = xTaskGetTickCount();
                }
                xQueueReset(button_queue); 
            }
        }
    }
}

void vMenuTask(void *pvParameters) {
    menu_params_t *params = (menu_params_t *)pvParameters;
    SSD1306_t *dev = params->display;
    pcnt_unit_handle_t pcnt_unit = params->pcnt_unit;

    estado_menu_t estado_actual = ESTADO_MENU_PRINCIPAL;
    int opcion_principal = 0; 
    int sub_opcion = 0;       
    int ultimo_conteo = 0;
    int conteo_actual = 0;
    bool refrescar_pantalla = true; 

    while (1) {
        // LEER EL ENCODER
        ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &conteo_actual));
        int diferencia = conteo_actual - ultimo_conteo;

        // DETECTAR EL GIRO
        if (diferencia >= 4 || diferencia <= -4) {
            int direccion = (diferencia >= 4) ? 1 : -1;

            if (estado_actual == ESTADO_MENU_PRINCIPAL) {
                opcion_principal += direccion;
                if (opcion_principal > 2) opcion_principal = 0; 
                if (opcion_principal < 0) opcion_principal = 2;
            } else {
                sub_opcion += direccion;
                if (sub_opcion > 2) sub_opcion = 0; 
                if (sub_opcion < 0) sub_opcion = 2;
            }
            ultimo_conteo = conteo_actual;
            refrescar_pantalla = true; 
        }

        // DETECTAR EL BOTÓN
        if (boton_presionado) {
            boton_presionado = false; 
            
            if (estado_actual == ESTADO_MENU_PRINCIPAL) {
                if (opcion_principal == 0) estado_actual = ESTADO_TIEMPO;
                else if (opcion_principal == 1) estado_actual = ESTADO_AMPLITUD;
                else if (opcion_principal == 2) estado_actual = ESTADO_TRIGGER;
                sub_opcion = 0; 
            } else {
                // GUARDAR CONFIGURACIÓN EN EL MAILBOX (COLA)
                config_osciloscopio_t nueva_config;
                xQueuePeek(config_queue, &nueva_config, 0); // Leemos el estado actual

                if (estado_actual == ESTADO_TIEMPO) {
                    if (sub_opcion == 0) nueva_config.tiempo_ms = 10;
                    else if (sub_opcion == 1) nueva_config.tiempo_ms = 50;
                    else if (sub_opcion == 2) nueva_config.tiempo_ms = 100;
                } 
                else if (estado_actual == ESTADO_AMPLITUD) {
                    if (sub_opcion == 0) nueva_config.amplitud_v = 1.0;
                    else if (sub_opcion == 1) nueva_config.amplitud_v = 3.3;
                    else if (sub_opcion == 2) nueva_config.amplitud_v = 5.0;
                } 
                else if (estado_actual == ESTADO_TRIGGER) {
                    if (sub_opcion == 0) nueva_config.flanco = FLANCO_ASCENDENTE;
                    else if (sub_opcion == 1) nueva_config.flanco = FLANCO_DESCENDENTE;
                }
                
                // Sobrescribimos el buzón de forma segura
                xQueueOverwrite(config_queue, &nueva_config);
                
                estado_actual = ESTADO_MENU_PRINCIPAL;
            }
            refrescar_pantalla = true; 
        }

        // DIBUJAR PANTALLA
        if (refrescar_pantalla) {
            ssd1306_clear_screen(dev, false);

            if (estado_actual == ESTADO_MENU_PRINCIPAL) {
                ssd1306_display_text(dev, 0, "OSCILOSCOPIO", 13, false);
                ssd1306_display_text(dev, 2, " Tiempo/div", 11, opcion_principal == 0);
                ssd1306_display_text(dev, 3, " Amplitud/div", 13, opcion_principal == 1);
                ssd1306_display_text(dev, 4, " Trigger", 8, opcion_principal == 2);
            }
            else if (estado_actual == ESTADO_TIEMPO) {
                ssd1306_display_text(dev, 0, "> TIEMPO/DIV", 12, false);
                ssd1306_display_text(dev, 2, " 10 ms", 6, sub_opcion == 0);
                ssd1306_display_text(dev, 3, " 50 ms", 6, sub_opcion == 1);
                ssd1306_display_text(dev, 4, " 100 ms", 7, sub_opcion == 2);
            }
            else if (estado_actual == ESTADO_AMPLITUD) {
                ssd1306_display_text(dev, 0, "> AMPLITUD", 10, false);
                ssd1306_display_text(dev, 2, " 1 V/div", 8, sub_opcion == 0);
                ssd1306_display_text(dev, 3, " 3.3 V/div", 10, sub_opcion == 1);
                ssd1306_display_text(dev, 4, " 5 V/div", 8, sub_opcion == 2);
            }
            else if (estado_actual == ESTADO_TRIGGER) {
                ssd1306_display_text(dev, 0, "> TRIGGER", 9, false);
                ssd1306_display_text(dev, 2, " Flanco Subida", 14, sub_opcion == 0);
                ssd1306_display_text(dev, 3, " Flanco Bajada", 14, sub_opcion == 1);
                ssd1306_display_text(dev, 4, " Apagado", 8, sub_opcion == 2);
            }

            ssd1306_show_buffer(dev);
            refrescar_pantalla = false; 
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ====================================================================
// 5. TAREAS DEL MOTOR (ADC, TRIGGER y UART)
// ====================================================================
static void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle) {
    adc_continuous_handle_t handle = NULL;
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 8192,
        .conv_frame_size = EXAMPLE_READ_LEN,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 20000, 
        .conv_mode = EXAMPLE_ADC_CONV_MODE,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2, 
    };
    
    adc_digi_pattern_config_t adc_pattern[1] = {0};
    dig_cfg.pattern_num = channel_num;
    adc_pattern[0].atten = EXAMPLE_ADC_ATTEN;
    adc_pattern[0].channel = channel[0] & 0x7;
    adc_pattern[0].unit = EXAMPLE_ADC_UNIT;
    adc_pattern[0].bit_width = EXAMPLE_ADC_BIT_WIDTH;
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));
    *out_handle = handle;
}

void vAdcTask(void *pvParameters) {
    esp_err_t ret;
    uint32_t ret_num = 0;
    uint16_t* buffer_activo = buffer_A; 
    int muestras_acumuladas = 0;

    adc_continuous_handle_t handle = NULL;
    continuous_adc_init(channel, 1, &handle); 

    adc_continuous_evt_cbs_t cbs = { .on_conv_done = s_conv_done_cb };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(handle));

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        while (1) {
            ret = adc_continuous_read(handle, hardware_read_buffer, EXAMPLE_READ_LEN, &ret_num, 0);
            
            if (ret == ESP_OK) {
                uint32_t num_parsed_samples = 0;
                esp_err_t parse_ret = adc_continuous_parse_data(handle, hardware_read_buffer, ret_num, parsed_data_buffer, &num_parsed_samples);
                
                if (parse_ret == ESP_OK) {
                    for (int i = 0; i < num_parsed_samples; i++) {
                        if (parsed_data_buffer[i].valid) {
                            buffer_activo[muestras_acumuladas] = parsed_data_buffer[i].raw_data;
                            muestras_acumuladas++;

                            if (muestras_acumuladas >= NUM_MUESTRAS) {
                                if (xQueueSend(full_queue, &buffer_activo, 0) == pdPASS) {
                                    buffer_activo = (buffer_activo == buffer_A) ? buffer_B : buffer_A;
                                }
                                muestras_acumuladas = 0;
                            }
                        }
                    }
                }
            } else if (ret == ESP_ERR_TIMEOUT) {
                break;
            }
        }
    }
}

void vTriggerTask(void *pvParameters) {
    uint16_t* buffer_a_procesar;
    TickType_t ultimo_dibujo = 0;
    
    // Variable local que copia la configuración actual
    config_osciloscopio_t mi_config;
    xQueuePeek(config_queue, &mi_config, portMAX_DELAY);

    while(1) {
        if (xQueueReceive(full_queue, &buffer_a_procesar, portMAX_DELAY) == pdTRUE) {
            
            // Actualizamos la configuración si el Menú mandó algo nuevo (NO bloqueante)
            config_osciloscopio_t temp_cfg;
            if (xQueuePeek(config_queue, &temp_cfg, 0) == pdTRUE) {
                mi_config = temp_cfg;
            }

            TickType_t ahora = xTaskGetTickCount();
            
            if ((ahora - ultimo_dibujo) > pdMS_TO_TICKS(150)) {
                int indice_del_disparo = -1;
                int inicio_busqueda = PUNTOS_PANTALLA / 2;
                int fin_busqueda = NUM_MUESTRAS - (PUNTOS_PANTALLA / 2);

                // Búsqueda del flanco usando mi_config
                for (int i = inicio_busqueda; i < fin_busqueda; i++) {
                    uint32_t anterior = buffer_a_procesar[i - 1];
                    uint32_t actual   = buffer_a_procesar[i];

                    if (mi_config.flanco == FLANCO_ASCENDENTE) {
                        if (anterior < (mi_config.nivel - HISTERESIS) && actual >= (mi_config.nivel + HISTERESIS)) {
                            indice_del_disparo = i;
                            break; 
                        }
                    } else {
                        if (anterior > (mi_config.nivel + HISTERESIS) && actual <= (mi_config.nivel - HISTERESIS)) {
                            indice_del_disparo = i;
                            break; 
                        }
                    }
                }

                if (indice_del_disparo == -1) {
                    indice_del_disparo = NUM_MUESTRAS / 2;
                }

                int inicio = indice_del_disparo - (PUNTOS_PANTALLA / 2);

                // Empaquetado Aislado para la UART usando mi_config
                trama_uart_t nueva_trama;
                nueva_trama.flanco = mi_config.flanco;
                nueva_trama.nivel = mi_config.nivel;
                nueva_trama.tiempo = mi_config.tiempo_ms;
                nueva_trama.amplitud = mi_config.amplitud_v;

                for(int i = 0; i < PUNTOS_PANTALLA; i++) {
                    nueva_trama.ventana[i] = buffer_a_procesar[inicio + i];
                }

                xQueueSend(Uart_queue, &nueva_trama, 0);
                ultimo_dibujo = xTaskGetTickCount(); 
            }
        }
    }
}

void vUartTask(void *pvParameters) {
    trama_uart_t trama;

    while(1) {
        if (xQueueReceive(Uart_queue, &trama, portMAX_DELAY) == pdTRUE) {
            printf("SYNC,%d,%"PRIu32",%"PRIu32",%.1f\n", 
                    trama.flanco, trama.nivel, trama.tiempo, trama.amplitud);

            for (int i = 0; i < PUNTOS_PANTALLA; i++) {
                printf("%"PRIu16"\n", trama.ventana[i]);
            }
        }
    }
}

// ====================================================================
// 6. INICIALIZACIONES SECUNDARIAS
// ====================================================================
static void init_pwm_test_signal(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_10_BIT, 
        .freq_hz          = PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PWM_OUTPUT_GPIO,
        .duty           = 512, 
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

// ====================================================================
// 7. FUNCION PRINCIPAL (DIRECTOR DE ORQUESTA)
// ====================================================================
void app_main(void)
{
    // --- Interfaces Crudas ---
    init_pwm_test_signal();
    uart_set_baudrate(UART_NUM_0, 921600);

    // --- Colas ---
    full_queue = xQueueCreate(1, sizeof(uint16_t*));
    Uart_queue = xQueueCreate(1, sizeof(trama_uart_t)); 
    button_queue = xQueueCreate(10, sizeof(uint32_t));
    
    // Cola Mailbox de configuración
    config_queue = xQueueCreate(1, sizeof(config_osciloscopio_t));
    config_osciloscopio_t config_inicial = {
        .flanco = FLANCO_DESCENDENTE,
        .nivel = 2000,
        .tiempo_ms = 50,
        .amplitud_v = 3.3
    };
    xQueueSend(config_queue, &config_inicial, 0);

    // --- Encoder PCNT ---
    gpio_set_pull_mode(EXAMPLE_EC11_GPIO_A, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(EXAMPLE_EC11_GPIO_B, GPIO_PULLUP_ONLY);

    pcnt_unit_config_t unit_config = {
        .high_limit = EXAMPLE_PCNT_HIGH_LIMIT,
        .low_limit = EXAMPLE_PCNT_LOW_LIMIT,
    };
    pcnt_unit_handle_t pcnt_unit = NULL;
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 2000 };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    pcnt_chan_config_t chan_a_config = { .edge_gpio_num = EXAMPLE_EC11_GPIO_A, .level_gpio_num = EXAMPLE_EC11_GPIO_B };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    pcnt_chan_config_t chan_b_config = { .edge_gpio_num = EXAMPLE_EC11_GPIO_B, .level_gpio_num = EXAMPLE_EC11_GPIO_A };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    // --- Pantalla OLED ---
    static SSD1306_t dev;
#if CONFIG_I2C_INTERFACE
    ESP_LOGI(TAG, "Iniciando interfaz I2C");
    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
#endif
    ssd1306_init(&dev, 128, 64);
    ssd1306_contrast(&dev, 0xff);
    ssd1306_clear_screen(&dev, false);

    // --- Botón ---
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    ESP_ERROR_CHECK(gpio_config(&btn_conf));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, (void *)BUTTON_GPIO);

    // --- Parámetros de Tareas ---
    static menu_params_t menu_parameters;
    menu_parameters.display = &dev;
    menu_parameters.pcnt_unit = pcnt_unit;

    // --- Despliegue de Tareas ---
    
    // Core 0: Adquisición Ininterrumpida
    xTaskCreatePinnedToCore(vAdcTask, "vAdcTask", 8192, NULL, 4, &xAdcTaskHandle, 0);
    
    // Core 1: Procesamiento, UI y Comunicación
    xTaskCreatePinnedToCore(vTriggerTask, "vTriggerTask", 8192, NULL, 4, &xTriggerTaskHandle, 1);
    xTaskCreatePinnedToCore(vUartTask, "vUartTask", 4096, NULL, 3, &xUartTaskHandle, 1); 
    xTaskCreatePinnedToCore(vButtonTask, "vButtonTask", 2048, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(vMenuTask, "vMenuTask", 4096, &menu_parameters, 2, NULL, 1);

    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}