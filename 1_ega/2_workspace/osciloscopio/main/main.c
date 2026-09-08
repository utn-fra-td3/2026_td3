#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
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
#include "driver/uart.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "OSCILOSCOPIO_FINAL";

// ====================================================================
// 1. CONFIGURACIONES DE HARDWARE Y PINES
// ====================================================================
// Encoder y Botón
#define EXAMPLE_PCNT_HIGH_LIMIT 100
#define EXAMPLE_PCNT_LOW_LIMIT  -100
#define EXAMPLE_EC11_GPIO_A 6
#define EXAMPLE_EC11_GPIO_B 5
#define BUTTON_GPIO 4

// ADC
#define EXAMPLE_ADC_UNIT             ADC_UNIT_1
#define EXAMPLE_ADC_CONV_MODE        ADC_CONV_SINGLE_UNIT_1
#define EXAMPLE_ADC_ATTEN            ADC_ATTEN_DB_12
#define EXAMPLE_ADC_BIT_WIDTH        SOC_ADC_DIGI_MAX_BITWIDTH
#define EXAMPLE_READ_LEN             4096 
#define NUM_MUESTRAS                 8192 // Ampliado para el diezmado
#define PUNTOS_PANTALLA              400 
#define HISTERESIS                   100
// Máximo documentado para ADC continuo en ESP32-S3 es 83333 Hz (SOC_ADC_SAMPLE_FREQ_THRES_HIGH).
// Usamos 80000 Hz dejando margen.
#define ADC_SAMPLE_FREQ_HZ           80000

//UART

#define UART_SYNC 0xAA55
#define UART_SYNC_PC 0xBB66


// NVS (flash no volátil) para persistir la última config del menú
#define NVS_NAMESPACE    "oscilo"
#define NVS_KEY_CONFIG   "config"

// ====================================================================
// 2. ESTRUCTURAS, TIPOS Y VARIABLES GLOBALES
// ====================================================================
typedef enum { 
    ESTADO_MENU_PRINCIPAL, 
    ESTADO_TIEMPO, 
    ESTADO_AMPLITUD, 
    ESTADO_FLANCO, 
    ESTADO_NIVEL_TRIG,
    ESTADO_MODO 
} estado_menu_t;

typedef enum { FLANCO_ASCENDENTE = 0, FLANCO_DESCENDENTE = 1 } tipo_flanco_t;
typedef enum { MODO_X1 = 0, MODO_X10 = 1, MODO_AC = 2 } modo_atenuacion_t; 

// Estructura para el Mailbox de Configuración
typedef struct {
    tipo_flanco_t flanco;
    uint32_t nivel;
    uint32_t tiempo_ms;
    float amplitud_v;
    modo_atenuacion_t modo; 
} config_osciloscopio_t;

// Estructura de parámetros para el menú
typedef struct {
    SSD1306_t *display;
} menu_params_t;

// Estructura de parámetros para la tarea del encoder
typedef struct {
    pcnt_unit_handle_t pcnt_unit;
} encoder_params_t;

// Estructura para empaquetar datos hacia la UART
typedef struct {
    uint16_t ventana[PUNTOS_PANTALLA];
    uint8_t flanco;
    uint32_t nivel;
    uint32_t tiempo;
    float amplitud;
    modo_atenuacion_t modo; 
} trama_uart_t;

// ====================================================================
// EVENTOS HACIA vMenuTask
// ====================================================================

typedef enum {
    EVT_BOTON_PRESIONADO,
    EVT_ENCODER,
    EVT_COMANDO_UART,
    EVT_FLASH_CARGADA
} menu_evento_tipo_t;

typedef struct {
    menu_evento_tipo_t tipo;
    union {
        int encoder_delta;                    // válido para EVT_ENCODER (+1 / -1)
        config_osciloscopio_t config_recibida; // válido para EVT_COMANDO_UART y EVT_FLASH_CARGADA
    } data;
    bool flash_encontrada; // válido solo para EVT_FLASH_CARGADA
} menu_evento_t;

// ====================================================================
// COMANDOS HACIA vFlash
// ====================================================================

typedef enum {
    FLASH_CMD_CARGAR,
    FLASH_CMD_GUARDAR
} flash_cmd_tipo_t;

typedef struct {
    flash_cmd_tipo_t tipo;
    config_osciloscopio_t config; // válido solo para FLASH_CMD_GUARDAR
} flash_cmd_t;

// ====================================================================
// PROTOCOLO BINARIO DE TRANSMISIÓN (reemplaza el printf en ASCII)
// ====================================================================


typedef struct __attribute__((packed)) {
    uint16_t sync;
    uint8_t  flanco;
    uint16_t nivel;
    uint16_t tiempo_ms;
    float    amplitud;
    uint8_t  modo;
    uint16_t num_puntos;
    uint16_t datos[PUNTOS_PANTALLA];
    uint16_t checksum;
} trama_binaria_t;

typedef struct __attribute__((packed)) {
    uint16_t sync;
    uint8_t  flanco;
    uint32_t nivel;
    uint32_t tiempo_ms;
    float    amplitud_v;
    uint8_t  modo;
    uint16_t checksum;
} comando_pc_t;

// Colas del Sistema
static QueueHandle_t full_queue = NULL;
static QueueHandle_t Uart_queue = NULL;
static QueueHandle_t config_queue = NULL; // Mailbox
static QueueHandle_t menu_evento_queue = NULL; // Eventos hacia vMenuTask (vFlash, vEncoder, vComandoUartTask, vButtonTask)
static QueueHandle_t flash_cmd_queue = NULL;   // Pedidos de vMenuTask hacia vFlash

// Semáforos
static SemaphoreHandle_t button_semaphore = NULL; // Entregado por la ISR del botón

// Manejadores de Tareas y Tiempos
static TaskHandle_t xAdcTaskHandle = NULL;
static TaskHandle_t xTriggerTaskHandle = NULL;
static TaskHandle_t xUartTaskHandle = NULL;

// Buffers del ADC
static uint8_t hardware_read_buffer[EXAMPLE_READ_LEN];
static adc_continuous_data_t parsed_data_buffer[EXAMPLE_READ_LEN / SOC_ADC_DIGI_RESULT_BYTES];
static uint16_t buffer_A[NUM_MUESTRAS];
static uint16_t buffer_B[NUM_MUESTRAS];

// ====================================================================
// PERSISTENCIA EN FLASH (NVS): guarda/carga config_osciloscopio_t como un blob
// ====================================================================
static void guardar_config_en_flash(const config_osciloscopio_t *cfg) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_blob(handle, NVS_KEY_CONFIG, cfg, sizeof(config_osciloscopio_t));
    nvs_commit(handle);
    nvs_close(handle);
}

// Devuelve true si encontró y cargó una config guardada previamente
static bool cargar_config_de_flash(config_osciloscopio_t *cfg) {
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;

    size_t tamano = sizeof(config_osciloscopio_t);
    esp_err_t err = nvs_get_blob(handle, NVS_KEY_CONFIG, cfg, &tamano);
    nvs_close(handle);

    return (err == ESP_OK && tamano == sizeof(config_osciloscopio_t));
}


// ====================================================================
// 3. RUTINAS DE SERVICIO DE INTERRUPCIÓN (ISRs)
// ====================================================================
static void IRAM_ATTR button_isr_handler(void *arg) {

    gpio_intr_disable(BUTTON_GPIO);

    BaseType_t mustYield = pdFALSE;
    xSemaphoreGiveFromISR(button_semaphore, &mustYield);
    portYIELD_FROM_ISR(mustYield);
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
    while (1) {
        // Bloqueada en el semáforo sin consumir CPU hasta que la ISR lo entregue
        if (xSemaphoreTake(button_semaphore, portMAX_DELAY) == pdTRUE) {

            menu_evento_t evt;
            evt.tipo = EVT_BOTON_PRESIONADO;
            xQueueSend(menu_evento_queue, &evt, portMAX_DELAY);

            bool nivel_estable = false;
            while (!nivel_estable) {
                while (gpio_get_level(BUTTON_GPIO) == 0) {
                    vTaskDelay(pdMS_TO_TICKS(10)); // sigue presionado (o rebotando)
                }
                vTaskDelay(pdMS_TO_TICKS(20)); // margen de confirmación
                if (gpio_get_level(BUTTON_GPIO) == 1) nivel_estable = true;
            }

            // Recién ahora, con el botón soltado y estable, rehabilitamos
            gpio_intr_enable(BUTTON_GPIO);
        }
    }
}

void vEncoder(void *pvParameters) {
    encoder_params_t *params = (encoder_params_t *)pvParameters;
    pcnt_unit_handle_t pcnt_unit = params->pcnt_unit;

    int ultimo_conteo = 0;
    int conteo_actual = 0;

    while (1) {
        ESP_ERROR_CHECK(pcnt_unit_get_count(pcnt_unit, &conteo_actual));
        int diferencia = conteo_actual - ultimo_conteo;

        if (diferencia >= 4 || diferencia <= -4) {
            int direccion = (diferencia >= 4) ? 1 : -1;
            ultimo_conteo = conteo_actual;

            menu_evento_t evt;
            evt.tipo = EVT_ENCODER;
            evt.data.encoder_delta = direccion;
            xQueueSend(menu_evento_queue, &evt, portMAX_DELAY);
        }

        // Mismo período que usaba antes el polling del encoder dentro de
        // vMenuTask (vTaskDelay(30) al final del while(1)).
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}


void vFlash(void *pvParameters) {
    flash_cmd_t cmd;
    while (1) {
        if (xQueueReceive(flash_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd.tipo == FLASH_CMD_CARGAR) {
                config_osciloscopio_t cfg_cargada;
                bool encontrada = cargar_config_de_flash(&cfg_cargada);

                menu_evento_t evt;
                evt.tipo = EVT_FLASH_CARGADA;
                evt.flash_encontrada = encontrada;
                if (encontrada) {
                    evt.data.config_recibida = cfg_cargada;
                }
                xQueueSend(menu_evento_queue, &evt, portMAX_DELAY);
            }
            else if (cmd.tipo == FLASH_CMD_GUARDAR) {
                guardar_config_en_flash(&cmd.config);
            }
        }
    }
}

void vMenuTask(void *pvParameters) {
    menu_params_t *params = (menu_params_t *)pvParameters;
    SSD1306_t *dev = params->display;

    estado_menu_t estado_actual = ESTADO_MENU_PRINCIPAL;
    int opcion_principal = 0; 
    int sub_opcion = 0;       
    bool refrescar_pantalla = true; 
    

    config_osciloscopio_t config_activa = {
        .flanco = FLANCO_DESCENDENTE,
        .nivel = 2000,
        .tiempo_ms = 10, // 10 décimas = 1 ms/div, valor por defecto
        .amplitud_v = 1.0,
        .modo = MODO_X1 // <-- Arranca por defecto en canal 5 (GPIO 6)
    };
    xQueueOverwrite(config_queue, &config_activa);

    flash_cmd_t cmd_carga_inicial = { .tipo = FLASH_CMD_CARGAR };
    xQueueSend(flash_cmd_queue, &cmd_carga_inicial, portMAX_DELAY);

    while (1) {


        menu_evento_t evt;
        if (xQueueReceive(menu_evento_queue, &evt, portMAX_DELAY) == pdTRUE) {

            if (evt.tipo == EVT_ENCODER) {
                int direccion = evt.data.encoder_delta;

                if (estado_actual == ESTADO_MENU_PRINCIPAL) {
                    opcion_principal += direccion;
                    if (opcion_principal > 4) opcion_principal = 0; // Ahora hay 5 opciones (0 a 4)
                    if (opcion_principal < 0) opcion_principal = 4;
                } 
                else if (estado_actual == ESTADO_NIVEL_TRIG) {
                    if (direccion > 0 && config_activa.nivel <= 3800) config_activa.nivel += 200;
                    else if (direccion < 0 && config_activa.nivel >= 200) config_activa.nivel -= 200;
                    xQueueOverwrite(config_queue, &config_activa);
                }
                else {
                    sub_opcion += direccion;
                    
                    int max_opciones;
                    if (estado_actual == ESTADO_TIEMPO) max_opciones = 3; // 4 opciones (0.5, 1, 2, 5 ms)
                    else if (estado_actual == ESTADO_AMPLITUD) max_opciones = 1; // 2 opciones
                    else if (estado_actual == ESTADO_FLANCO) max_opciones = 1; // 2 opciones
                    else if (estado_actual == ESTADO_MODO) max_opciones = 2; // 3 opciones (X1, X10, AC)
                    else max_opciones = 0;

                    if (sub_opcion > max_opciones) sub_opcion = 0; 
                    if (sub_opcion < 0) sub_opcion = max_opciones;
                }
            }
            else if (evt.tipo == EVT_BOTON_PRESIONADO) {

                if (estado_actual == ESTADO_MENU_PRINCIPAL) {
                    if (opcion_principal == 0) estado_actual = ESTADO_TIEMPO;
                    else if (opcion_principal == 1) estado_actual = ESTADO_AMPLITUD;
                    else if (opcion_principal == 2) estado_actual = ESTADO_FLANCO;
                    else if (opcion_principal == 3) estado_actual = ESTADO_NIVEL_TRIG;
                    else if (opcion_principal == 4) estado_actual = ESTADO_MODO;
                    sub_opcion = 0; 
                } else {
                    if (estado_actual == ESTADO_TIEMPO) {
                        // config_activa.tiempo_ms queda expresado en DÉCIMAS de ms
                        // (5=0.5ms, 10=1ms, 20=2ms, 50=5ms) para poder representar 0.5 sin usar float
                        if (sub_opcion == 0) config_activa.tiempo_ms = 5;
                        else if (sub_opcion == 1) config_activa.tiempo_ms = 10;
                        else if (sub_opcion == 2) config_activa.tiempo_ms = 20;
                        else if (sub_opcion == 3) config_activa.tiempo_ms = 50;
                    } 
                    else if (estado_actual == ESTADO_AMPLITUD) {
                        if (sub_opcion == 0) config_activa.amplitud_v = 1.0;
                        else if (sub_opcion == 1) config_activa.amplitud_v = 5.0;
                    } 
                    else if (estado_actual == ESTADO_FLANCO) {
                        if (sub_opcion == 0) config_activa.flanco = FLANCO_ASCENDENTE;
                        else if (sub_opcion == 1) config_activa.flanco = FLANCO_DESCENDENTE;
                    }
                    else if (estado_actual == ESTADO_MODO) {
                        if (sub_opcion == 0) config_activa.modo = MODO_X1;
                        else if (sub_opcion == 1) config_activa.modo = MODO_X10;
                        else if (sub_opcion == 2) config_activa.modo = MODO_AC;
                    }
                    
                    xQueueOverwrite(config_queue, &config_activa);

                    flash_cmd_t cmd_guardar = { .tipo = FLASH_CMD_GUARDAR, .config = config_activa };
                    xQueueSend(flash_cmd_queue, &cmd_guardar, 0);

                    estado_actual = ESTADO_MENU_PRINCIPAL;
                }
            }
            else if (evt.tipo == EVT_COMANDO_UART) {

                config_activa = evt.data.config_recibida;
                xQueueOverwrite(config_queue, &config_activa);

                flash_cmd_t cmd_guardar = { .tipo = FLASH_CMD_GUARDAR, .config = config_activa };
                xQueueSend(flash_cmd_queue, &cmd_guardar, 0);
            }
            else if (evt.tipo == EVT_FLASH_CARGADA) {
                if (evt.flash_encontrada) {
                    config_activa = evt.data.config_recibida;
                    xQueueOverwrite(config_queue, &config_activa);
                } else {
                    ESP_LOGI(TAG, "No había config guardada en flash, sigo con los valores por defecto");
                }
            }

            refrescar_pantalla = true;
        }

        if (refrescar_pantalla) {
            ssd1306_clear_screen(dev, false);
            char buffer_str[32];

            if (estado_actual == ESTADO_MENU_PRINCIPAL) {
                ssd1306_display_text(dev, 0, "OSCILOSCOPIO", 13, false);
                ssd1306_display_text(dev, 2, " Tiempo/div", 11, opcion_principal == 0);
                ssd1306_display_text(dev, 3, " Amplitud/div", 13, opcion_principal == 1);
                ssd1306_display_text(dev, 4, " Flanco Trig", 12, opcion_principal == 2);
                ssd1306_display_text(dev, 5, " Nivel Trig", 11, opcion_principal == 3);
                ssd1306_display_text(dev, 6, " Modo Canal", 11, opcion_principal == 4);
            }
            else if (estado_actual == ESTADO_TIEMPO) {
                ssd1306_display_text(dev, 0, "> TIEMPO/DIV", 12, false);
                ssd1306_display_text(dev, 2, " 0.5 ms", 7, sub_opcion == 0);
                ssd1306_display_text(dev, 3, " 1 ms", 5, sub_opcion == 1);
                ssd1306_display_text(dev, 4, " 2 ms", 5, sub_opcion == 2);
                ssd1306_display_text(dev, 5, " 5 ms", 5, sub_opcion == 3);
            }
            else if (estado_actual == ESTADO_AMPLITUD) {
                ssd1306_display_text(dev, 0, "> AMPLITUD", 10, false);
                ssd1306_display_text(dev, 2, " 1.0 V/div", 10, sub_opcion == 0);
                ssd1306_display_text(dev, 3, " 5.0 V/div", 10, sub_opcion == 1);
            }
            else if (estado_actual == ESTADO_FLANCO) {
                ssd1306_display_text(dev, 0, "> FLANCO TRIG", 13, false);
                ssd1306_display_text(dev, 2, " Subida", 7, sub_opcion == 0);
                ssd1306_display_text(dev, 3, " Bajada", 7, sub_opcion == 1);
            }
            else if (estado_actual == ESTADO_NIVEL_TRIG) {
                ssd1306_display_text(dev, 0, "> NIVEL TRIG", 12, false);
                sprintf(buffer_str, " Nivel: %"PRIu32, config_activa.nivel);
                ssd1306_display_text(dev, 3, buffer_str, strlen(buffer_str), true);
                ssd1306_display_text(dev, 5, " (Girar p/ajuste)", 17, false);
            }
            else if (estado_actual == ESTADO_MODO) {
                ssd1306_display_text(dev, 0, "> MODO ENTRADA", 14, false);
                ssd1306_display_text(dev, 2, " x1  (Ch 5)", 11, sub_opcion == 0);
                ssd1306_display_text(dev, 3, " x10 (Ch 6)", 11, sub_opcion == 1);
                ssd1306_display_text(dev, 4, " AC  (Ch 2)", 11, sub_opcion == 2);
            }

            ssd1306_show_buffer(dev);
            refrescar_pantalla = false; 
        }
    }
}

// ====================================================================
// 5. TAREAS DEL MOTOR (ADC, TRIGGER y UART)
// ====================================================================

void vAdcTask(void *pvParameters) {
    esp_err_t ret; // Variable para almacenar el resultado de las funciones del ADC
    uint32_t ret_num = 0; // Variable para almacenar el número de bytes leídos del ADC
    uint16_t* buffer_activo = buffer_A; 
    int muestras_acumuladas = 0;

    adc_continuous_handle_t handle = NULL; // Manejador del ADC continuo
    modo_atenuacion_t modo_actual = 99; // Valor inicial inválido para forzar configuración
    config_osciloscopio_t cfg; // Variable para almacenar la configuración actual del osciloscopio

    while (1) {
        // Chequeo dinámico de cambios de MODO desde el menú
        if (xQueuePeek(config_queue, &cfg, 0) == pdTRUE) { // Non-blocking peek to check for updated configuration
            if (cfg.modo != modo_actual) {  // Chequea si se cambio el modo para cambiar el gpio del ADC.
                // Detenemos el ADC actual si existe
                if (handle != NULL) { // Si ya hay un handle activo, detenemos y desinicializamos
                    ESP_ERROR_CHECK(adc_continuous_stop(handle)); // Detenemos la lectura continua
                    ESP_ERROR_CHECK(adc_continuous_deinit(handle)); // Desinicializamos el handle
                }

                // Selección del canal físico según el modo
                adc_channel_t canal_activo = ADC_CHANNEL_0; // Default: MODO_X1 (GPIO 6)
                if (cfg.modo == MODO_X10) canal_activo = ADC_CHANNEL_1; // MODO_X10 (GPIO 7)
                else if (cfg.modo == MODO_AC) canal_activo = ADC_CHANNEL_6; // MODO_AC (GPIO 3)

                // Re-inicialización del ADC con el nuevo canal
                adc_continuous_handle_cfg_t adc_config = { // Configuración del handle del ADC continuo
                    .max_store_buf_size = 8192,
                    .conv_frame_size = EXAMPLE_READ_LEN, // Tamaño del frame de conversión
                };
                ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle)); // Creamos un nuevo handle para el ADC continuo

                adc_continuous_config_t dig_cfg = { // Configuración de la conversión digital
                    .sample_freq_hz = ADC_SAMPLE_FREQ_HZ, 
                    .conv_mode = EXAMPLE_ADC_CONV_MODE, // Modo de conversión (single unit) 
                    .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2, 
                };
                
                adc_digi_pattern_config_t adc_pattern[1] = {0}; // Configuración del patrón de conversión para un solo canal
                dig_cfg.pattern_num = 1; // Número de canales a usar (1 en este caso)
                adc_pattern[0].atten = EXAMPLE_ADC_ATTEN; // Configuración de atenuación
                adc_pattern[0].channel = canal_activo & 0x7; // Canal ADC (0-7)
                adc_pattern[0].unit = EXAMPLE_ADC_UNIT; // Unidad ADC (1 o 2)
                adc_pattern[0].bit_width = EXAMPLE_ADC_BIT_WIDTH; // Ancho de bits del ADC (máximo soportado por el SOC)
                dig_cfg.adc_pattern = adc_pattern; // Asignamos el patrón de conversión al handle
                ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));

                adc_continuous_evt_cbs_t cbs = { .on_conv_done = s_conv_done_cb }; // Registramos la función de callback para cuando se complete una conversión
                ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL)); // Registramos la función de callback para eventos del ADC
                ESP_ERROR_CHECK(adc_continuous_start(handle)); // Iniciamos la lectura continua del ADC

                modo_actual = cfg.modo; // Actualizamos el modo actual para reflejar el cambio
            }
        }

        // Lectura de datos. Timeout de 50ms para permitir refresco de configuración
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) > 0) { // Espera a que el ADC indique que hay datos listos cada 50ms
            while (1) {
                ret = adc_continuous_read(handle, hardware_read_buffer, EXAMPLE_READ_LEN, &ret_num, 0); // Timeout de 0 para no bloquear la tarea
                
                if (ret == ESP_OK) {
                    uint32_t num_parsed_samples = 0;
                    esp_err_t parse_ret = adc_continuous_parse_data(handle, hardware_read_buffer, ret_num, parsed_data_buffer, &num_parsed_samples);
                    
                    if (parse_ret == ESP_OK) {
                        for (int i = 0; i < num_parsed_samples; i++) {
                            if (parsed_data_buffer[i].valid) {
                                buffer_activo[muestras_acumuladas] = parsed_data_buffer[i].raw_data; // Guardamos la muestra en el buffer activo
                                muestras_acumuladas++;

                                if (muestras_acumuladas >= NUM_MUESTRAS) {
                                    if (xQueueSend(full_queue, &buffer_activo, 0) == pdPASS) { // Enviamos el buffer completo a la cola para que la tarea de trigger lo procese
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
}

void vTriggerTask(void *pvParameters) {
    uint16_t* buffer_a_procesar;
    TickType_t ultimo_dibujo = 0; // Tick count of the last time the oscilloscope was drawn
    
    config_osciloscopio_t mi_config; 
    xQueuePeek(config_queue, &mi_config, portMAX_DELAY); // Get the initial configuration from the mailbox

    while(1) {
        if (xQueueReceive(full_queue, &buffer_a_procesar, portMAX_DELAY) == pdTRUE) { // Wait indefinitely for a full buffer from the ADC task
            
            //actualiza configuracion si hay cambios en el Mailbox

            config_osciloscopio_t temp_cfg;
            if (xQueuePeek(config_queue, &temp_cfg, 0) == pdTRUE) { // Non-blocking peek to check for updated configuration
                mi_config = temp_cfg; // Update the local configuration if there's a new one
            }
            
            // actualia configuración de trigger y tiempo de muestreo según la configuración del usuario

            TickType_t ahora = xTaskGetTickCount(); // Get the current tick count
            
            if ((ahora - ultimo_dibujo) > pdMS_TO_TICKS(150)) { // Limit the drawing rate to avoid overwhelming the UART
                
                // mi_config.tiempo_ms está en DÉCIMAS de ms. Convertimos a "muestras
                // reales a saltear entre puntos mostrados" según el sample rate del ADC:
                // factor = (tiempo_div_ms) * fs / (PUNTOS_PANTALLA/10) / 1000
                //        = (tiempo_ms_decimas/10) * fs / (40 * 1000)
                //        = tiempo_ms_decimas * fs / 400000

                //BASE DE TIEMPOS: 0.5ms/div, 1ms/div, 2ms/div, 5ms/div

                int factor = (mi_config.tiempo_ms * ADC_SAMPLE_FREQ_HZ) / 400000; //Si la base de tiempo es muy rápida, el factor puede ser < 1, lo que no tiene sentido. Aseguramos al menos 1 muestra entre puntos mostrados
                if (factor < 1) factor = 1; // Aseguramos al menos 1 muestra entre puntos mostrados
                int muestras_necesarias = PUNTOS_PANTALLA * factor; //calcula las muestras necesarias para llenar la pantalla según el factor de muestreo

                int indice_del_disparo = -1; // Índice del disparo dentro del buffer_a_procesar. Inicialmente -1 (no encontrado)
                int inicio_busqueda = muestras_necesarias / 2; // Índice de inicio para la búsqueda del disparo
                int fin_busqueda = NUM_MUESTRAS - (muestras_necesarias / 2); // Índice de fin para la búsqueda del disparo

                //Si tengo 4mil datos, necesito que hayan 2mil antes y 2mil después del cruce del trigger.

                bool armado = false;
 
                if (mi_config.flanco == FLANCO_ASCENDENTE) {
                    for (int i = inicio_busqueda; i < fin_busqueda; i++) {
                        uint32_t muestra = buffer_a_procesar[i];
 
                        if (!armado) {
                            // Esperamos a que la señal baje lo suficiente para "rearmar"
                            if (muestra < (mi_config.nivel - HISTERESIS)) { 
                                armado = true;
                            }
                        } else {
                            // Ya armado: buscamos el cruce ascendente del nivel
                            if (muestra >= mi_config.nivel) {
                                indice_del_disparo = i;
                                break;
                            }
                        }
                    }
                } else { // FLANCO_DESCENDENTE
                    for (int i = inicio_busqueda; i < fin_busqueda; i++) {
                        uint32_t muestra = buffer_a_procesar[i];
 
                        if (!armado) {
                            // Esperamos a que la señal suba lo suficiente para "rearmar"
                            if (muestra > (mi_config.nivel + HISTERESIS)) {
                                armado = true;
                            }
                        } else {
                            // Ya armado: buscamos el cruce descendente del nivel
                            if (muestra <= mi_config.nivel) {
                                indice_del_disparo = i;
                                break;
                            }
                        }
                    }
                }

                if (indice_del_disparo == -1) {
                    indice_del_disparo = NUM_MUESTRAS / 2;
                }

                int inicio = indice_del_disparo - (muestras_necesarias / 2); //resta mitad de las muestras necesarias para centrar el disparo

                trama_uart_t nueva_trama; // Estructura para enviar la trama a la UART
                nueva_trama.flanco = mi_config.flanco;
                nueva_trama.nivel = mi_config.nivel;
                nueva_trama.tiempo = mi_config.tiempo_ms;
                nueva_trama.amplitud = mi_config.amplitud_v;
                nueva_trama.modo = mi_config.modo; // <-- Asignamos el modo actual a la trama

                for(int i = 0; i < PUNTOS_PANTALLA; i++) {
                    nueva_trama.ventana[i] = buffer_a_procesar[inicio + (i * factor)];
                }

                xQueueSend(Uart_queue, &nueva_trama, 0); // Enviamos la trama a la cola de UART para su transmisión
                ultimo_dibujo = xTaskGetTickCount(); 
            }
        }
    }
}



// Suma simple de 16 bits sobre el paquete (todo menos el propio campo checksum)
static uint16_t calcular_checksum(const uint8_t *buf, size_t len) {
    uint16_t suma = 0;
    for (size_t i = 0; i < len; i++) suma += buf[i];
    return suma;
}

// Arma el paquete binario a partir de la trama interna y lo manda por UART0
static void enviar_trama_binaria(const trama_uart_t *trama) {
    trama_binaria_t paquete;
    paquete.sync       = UART_SYNC; // Little Endian: 0x55, 0xAA
    paquete.flanco     = trama->flanco;
    paquete.nivel      = (uint16_t)trama->nivel;
    paquete.tiempo_ms  = (uint16_t)trama->tiempo; // en DÉCIMAS de ms (ver nota en config_osciloscopio_t)
    paquete.amplitud   = trama->amplitud;
    paquete.modo       = (uint8_t)trama->modo;
    paquete.num_puntos = PUNTOS_PANTALLA;
    memcpy(paquete.datos, trama->ventana, sizeof(paquete.datos)); // Copiamos los datos de la ventana a la estructura del paquete

    size_t len_checksum = sizeof(paquete) - sizeof(paquete.checksum); // Calculamos el tamaño del paquete sin incluir el campo checksum
    paquete.checksum = calcular_checksum((uint8_t *)&paquete, len_checksum);

    uart_write_bytes(UART_NUM_0, (const char *)&paquete, sizeof(paquete));
}

void vComandoUartTask(void *pvParameters) {
    uint8_t buffer_rx[sizeof(comando_pc_t)];  // Buffer para recibir el comando completo desde la UART
    int rx_index = 0; // Índice para rastrear cuántos bytes se han recibido
    comando_pc_t *cmd; // Puntero a la estructura de comando que se llenará con los datos recibidos

    while (1) {
        // Lee 1 byte. Si no hay nada, la tarea duerme (0% CPU)
        if (uart_read_bytes(UART_NUM_0, &buffer_rx[rx_index], 1, portMAX_DELAY) > 0) {
            
            // Máquina de estados simple para encontrar el SYNC (0xBB66 -> Little Endian: 0x66, 0xBB)
            if (rx_index == 0 && buffer_rx[0] != (UART_SYNC_PC & 0xFF)) continue;
            if (rx_index == 1 && buffer_rx[1] != (UART_SYNC_PC >> 8)) { rx_index = 0; continue; }

            rx_index++;

            // ¿Recibimos el paquete completo?
            if (rx_index == sizeof(comando_pc_t)) {
                cmd = (comando_pc_t *)buffer_rx;
                
                // Calculamos checksum ignorando los últimos 2 bytes (que son el checksum recibido)
                uint16_t chk_calculado = calcular_checksum(buffer_rx, sizeof(comando_pc_t) - 2);

                if (chk_calculado == cmd->checksum) {

                    config_osciloscopio_t nueva_config = {
                        .flanco = cmd->flanco,
                        .nivel = cmd->nivel,
                        .tiempo_ms = cmd->tiempo_ms,
                        .amplitud_v = cmd->amplitud_v,
                        .modo = cmd->modo
                    };
                    menu_evento_t evt;
                    evt.tipo = EVT_COMANDO_UART;
                    evt.data.config_recibida = nueva_config;
                    xQueueSend(menu_evento_queue, &evt, portMAX_DELAY);
                }
                
                rx_index = 0; // Reiniciamos para buscar el próximo comando
            }
        }
    }
}


void vUartTask(void *pvParameters) {
    trama_uart_t trama;

    while(1) {
        if (xQueueReceive(Uart_queue, &trama, portMAX_DELAY) == pdTRUE) {
            enviar_trama_binaria(&trama);
        }
    }
}

// ====================================================================
// 6. FUNCION PRINCIPAL (DIRECTOR DE ORQUESTA)
// ====================================================================
void app_main(void)
{

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    uart_set_baudrate(UART_NUM_0, 921600);

    uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0);


    esp_log_level_set("*", ESP_LOG_NONE);

    full_queue = xQueueCreate(1, sizeof(uint16_t*));
    Uart_queue = xQueueCreate(1, sizeof(trama_uart_t)); 
    button_semaphore = xSemaphoreCreateBinary();
    
    config_queue = xQueueCreate(1, sizeof(config_osciloscopio_t));


    menu_evento_queue = xQueueCreate(5, sizeof(menu_evento_t));
    flash_cmd_queue = xQueueCreate(1, sizeof(flash_cmd_t));

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

    // 7. Inicializar Pantalla SSD1306
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

    static encoder_params_t encoder_parameters;
    encoder_parameters.pcnt_unit = pcnt_unit;

    // --- Despliegue de Tareas ---

    xTaskCreatePinnedToCore(vComandoUartTask, "vComandoUartTask", 4096, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(vAdcTask, "vAdcTask", 8192, NULL, 4, &xAdcTaskHandle, 1);
    xTaskCreatePinnedToCore(vTriggerTask, "vTriggerTask", 8192, NULL, 4, &xTriggerTaskHandle, 1);
    xTaskCreatePinnedToCore(vUartTask, "vUartTask", 4096, NULL, 3, &xUartTaskHandle, 1); 
    xTaskCreatePinnedToCore(vButtonTask, "vButtonTask", 2048, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(vMenuTask, "vMenuTask", 4096, &menu_parameters, 2, NULL, 1);
    xTaskCreatePinnedToCore(vEncoder, "vEncoder", 2048, &encoder_parameters, 2, NULL, 1);
    xTaskCreatePinnedToCore(vFlash, "vFlash", 4096, NULL, 1, NULL, 1);

    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}