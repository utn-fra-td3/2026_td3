#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/spi_master.h"
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_task_wdt.h"

static const char *TAG = "SISTEMA_DDS_COMPLETO";

// --- MAPEADO DE HARDWARE ---
#define PIN_SPI_MOSI     13
#define PIN_SPI_CLK      14
#define PIN_SPI_CS       15

#define PIN_ENC_CLK      18
#define PIN_ENC_DT       5
#define PIN_ENC_SW       19   // Botón Encoder: Cambia el PASO de frecuencia
#define PIN_BOTON_MODO   4    // Botón Extra: Cambia la FORMA de onda

#define PIN_I2C_SCL      22
#define PIN_I2C_SDA      21
#define OLED_I2C_ADDR    0x3C

// UART Configuración (Nativa para Comandos Remotos)
#define UART_PORT_NUM      UART_NUM_0
#define UART_BUF_SIZE      1024
#define PIN_UART_TX        UART_PIN_NO_CHANGE
#define PIN_UART_RX        UART_PIN_NO_CHANGE

// Parámetros DDS
#define SAMPLING_FREQ      25000      
#define POINTS_TABLE       64       

// --- ESTRUCTURAS DE DATOS Y MANEJADORES RTOS ---
typedef struct {
    uint32_t frecuencia;
    uint32_t modo;
    uint32_t paso;
} oled_msg_t;

static QueueHandle_t oled_queue = NULL;
static QueueHandle_t uart_rx_queue = NULL;
static TaskHandle_t xInputsTaskHandle = NULL; 

static spi_device_handle_t spi_dac_handle;
static i2c_master_dev_handle_t oled_dev_handle;

// Variables Globales DDS
static uint16_t tabla_seno[POINTS_TABLE];
static uint16_t tabla_cuadrada[POINTS_TABLE];
static uint16_t tabla_triangular[POINTS_TABLE];

static volatile uint32_t modo_onda = 0;          
static volatile uint32_t frecuencia_actual = 1000; 
static volatile uint32_t incremento_fase = 0;
static uint32_t acumulador_fase = 0;
static volatile uint32_t paso_frecuencia = 100; 

static volatile uint32_t ultimo_tiempo_enc_isr = 0;

// Matriz de fuente de texto básica de 5x7 fija para el OLED
const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5f, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00},
    {0x14, 0x7f, 0x14, 0x7f, 0x14}, {0x24, 0x2a, 0x7f, 0x2a, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62},
    {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00}, {0x00, 0x1c, 0x22, 0x41, 0x00},
    {0x00, 0x41, 0x22, 0x1c, 0x00}, {0x14, 0x08, 0x3e, 0x08, 0x14}, {0x08, 0x08, 0x3e, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00},
    {0x20, 0x10, 0x08, 0x04, 0x02}, {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10},
    {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x00, 0x36, 0x36, 0x00, 0x00},
    {0x00, 0x56, 0x36, 0x00, 0x00}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x24, 0x24, 0x24, 0x24, 0x24},
    {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06}, {0x32, 0x49, 0x79, 0x41, 0x3e},
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36}, {0x3e, 0x41, 0x41, 0x41, 0x22},
    {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f}, {0x00, 0x41, 0x7f, 0x41, 0x00},
    {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f}, {0x3e, 0x41, 0x41, 0x41, 0x3e},
    {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01}, {0x3f, 0x40, 0x40, 0x40, 0x3f},
    {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
};

// --- LOGICA DE PERSISTENCIA (NVS) ---
void guardar_estado_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_u32(my_handle, "frecuencia", frecuencia_actual);
        nvs_set_u32(my_handle, "modo", modo_onda);
        nvs_set_u32(my_handle, "paso", paso_frecuencia);
        nvs_commit(my_handle);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Configuracion respaldada en Flash NVS.");
    }
}

void cargar_estado_nvs(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        nvs_get_u32(my_handle, "frecuencia", (uint32_t*)&frecuencia_actual);
        nvs_get_u32(my_handle, "modo", (uint32_t*)&modo_onda);
        nvs_get_u32(my_handle, "paso", (uint32_t*)&paso_frecuencia);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Estado recuperado: %ld Hz, Modo: %ld, Paso: %ld", frecuencia_actual, modo_onda, paso_frecuencia);
    } else {
        ESP_LOGW(TAG, "Sin registros previos en NVS. Usando valores por defecto (1kHz).");
    }
}

// --- DRIVER PANTALLA OLED SSD1306 (I2C v6.0) ---
void oled_enviar_comando(uint8_t cmd) {
    uint8_t buffer[2] = {0x00, cmd};
    i2c_master_transmit(oled_dev_handle, buffer, 2, -1);
}
void oled_enviar_datos(uint8_t *data, size_t len) {
    uint8_t buffer[len + 1];
    buffer[0] = 0x40;
    memcpy(&buffer[1], data, len);
    i2c_master_transmit(oled_dev_handle, buffer, len + 1, -1);
}
void oled_inicializar(void) {
    oled_enviar_comando(0xAE); oled_enviar_comando(0xD5); oled_enviar_comando(0x80);
    oled_enviar_comando(0xA8); oled_enviar_comando(0x3F); oled_enviar_comando(0xD3);
    oled_enviar_comando(0x00); oled_enviar_comando(0x40); oled_enviar_comando(0x8D);
    oled_enviar_comando(0x14); oled_enviar_comando(0x20); oled_enviar_comando(0x02);
    oled_enviar_comando(0xA1); oled_enviar_comando(0xC8); oled_enviar_comando(0xDA);
    oled_enviar_comando(0x12); oled_enviar_comando(0x81); oled_enviar_comando(0xCF);
    oled_enviar_comando(0xD9); oled_enviar_comando(0xF1); oled_enviar_comando(0xDB);
    oled_enviar_comando(0x40); oled_enviar_comando(0xA4); oled_enviar_comando(0xA6);
    oled_enviar_comando(0xAF);
}
void oled_limpiar(void) {
    uint8_t buffer[128] = {0};
    for (uint8_t i = 0; i < 8; i++) {
        oled_enviar_comando(0xB0 + i); oled_enviar_comando(0x00); oled_enviar_comando(0x10);
        oled_enviar_datos(buffer, 128);
    }
}
void oled_escribir_texto(uint8_t pagina, uint8_t col, const char *txt) {
    oled_enviar_comando(0xB0 + pagina);
    oled_enviar_comando(col & 0x0F); oled_enviar_comando(0x10 | ((col >> 4) & 0x0F));
    while (*txt) {
        uint8_t c = *txt - 0x20;
        if (c < 96) {
            uint8_t char_buf[6];
            memcpy(char_buf, font5x7[c], 5); char_buf[5] = 0x00;
            oled_enviar_datos(char_buf, 6);
        }
        txt++;
    }
}

// --- LÓGICA GENERACIÓN DDS Y TIMER HARDWARE ---
uint16_t preparar_trama_mcp4921(uint16_t valor_digital) {
    uint16_t trama = 0x3000 | (valor_digital & 0x0FFF);
    return (trama << 8) | (trama >> 8);
}
static inline uint32_t calcular_incremento_fase(uint32_t frec_hz) {
    return (uint32_t)(((double)frec_hz * 4294967296.0) / SAMPLING_FREQ);
}

// ISR del Temporizador a 25 kHz (Fuera del scheduler)
static bool IRAM_ATTR dac_timer_isr_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx) {
    acumulador_fase += incremento_fase;      
    uint32_t indice = acumulador_fase >> 26;  

    spi_transaction_t trans = {.length = 16};
    if (modo_onda == 0)       trans.tx_buffer = &tabla_seno[indice];
    else if (modo_onda == 1)  trans.tx_buffer = &tabla_cuadrada[indice];
    else                      trans.tx_buffer = &tabla_triangular[indice];

    spi_device_polling_transmit(spi_dac_handle, &trans);
    return false; 
}

// ISR DEL ENCODER ROTATIVO
static void IRAM_ATTR encoder_gpio_isr_handler(void* arg) {
    uint32_t tiempo_actual = esp_timer_get_time();
    if (tiempo_actual - ultimo_tiempo_enc_isr > 35000) { // Debounce 35ms
        int nivel_dt = gpio_get_level(PIN_ENC_DT);
        if (nivel_dt == 1) {
            if (frecuencia_actual + paso_frecuencia <= 20000) frecuencia_actual += paso_frecuencia;
            else frecuencia_actual = 20000;
        } else {
            if (frecuencia_actual > paso_frecuencia) frecuencia_actual -= paso_frecuencia;
            else frecuencia_actual = 1;
        }
        
        
        incremento_fase = (uint32_t)(((double)frecuencia_actual * 4294967296.0) / SAMPLING_FREQ);
        ultimo_tiempo_enc_isr = tiempo_actual;

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(xInputsTaskHandle, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
    }
}



// TAREA 1:
void vTaskUART(void *pvParameters) {
    uint8_t *data = (uint8_t *) malloc(UART_BUF_SIZE);
    char cmd_line[64];
    int idx = 0;

    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, data, 1, portMAX_DELAY);
        if (len > 0) {
            char c = data[0];
            if (c == '\n' || c == '\r') {
                if (idx > 0) {
                    cmd_line[idx] = '\0';
                    xQueueSend(uart_rx_queue, &cmd_line, portMAX_DELAY);
                    idx = 0;
                }
            } else if (idx < sizeof(cmd_line) - 1) {
                cmd_line[idx++] = c;
            }
        }
    }
    free(data);
}

// TAREA 2:
void vTaskAnalizador(void *pvParameters) {
    char cmd[64];
    char resp[128];

    while (1) {
        if (xQueueReceive(uart_rx_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (strncmp(cmd, "SET:FREQ:", 9) == 0) {
                uint32_t f = atoi(&cmd[9]);
                if (f >= 1 && f <= 20000) {
                    frecuencia_actual = f;
                    incremento_fase = calcular_incremento_fase(frecuencia_actual);
                    snprintf(resp, sizeof(resp), "OK: FREQ SET TO %ld HZ\r\n", frecuencia_actual);
                } else {
                    snprintf(resp, sizeof(resp), "ERROR: FREQ OUT OF RANGE (1-20000 HZ)\r\n");
                }
                uart_write_bytes(UART_PORT_NUM, resp, strlen(resp));
            } 
            else if (strncmp(cmd, "SET:WAVE:", 9) == 0) {
                uint32_t m = atoi(&cmd[9]);
                if (m <= 2) {
                    modo_onda = m;
                    snprintf(resp, sizeof(resp), "OK: WAVE MODE SET TO %ld\r\n", modo_onda);
                } else {
                    snprintf(resp, sizeof(resp), "ERROR: INVALID MODE (0=SEN, 1=CUA, 2=TRI)\r\n");
                }
                uart_write_bytes(UART_PORT_NUM, resp, strlen(resp));
            }
            else if (strcmp(cmd, "GET:STATUS") == 0) {
                snprintf(resp, sizeof(resp), "STATUS -> FREQ: %ld Hz | ONDA: %ld | STEP: %ld Hz\r\n", 
                         frecuencia_actual, modo_onda, paso_frecuencia);
                uart_write_bytes(UART_PORT_NUM, resp, strlen(resp));
            }
            else {
                snprintf(resp, sizeof(resp), "ERROR: COMMAND NOT RECOGNIZED\r\n");
                uart_write_bytes(UART_PORT_NUM, resp, strlen(resp));
            }

            oled_msg_t msg = {.frecuencia = frecuencia_actual, .modo = modo_onda, .paso = paso_frecuencia};
            xQueueOverwrite(oled_queue, &msg);
            guardar_estado_nvs(); // Guarda cambios remotos
        }
    }
}

// TAREA 3:
void vTaskInputMonitor(void *pvParameters) {
    esp_task_wdt_add(NULL); // Registrar tarea en el Watchdog anti-fallos
    
    int ultimo_sw = 1;
    int ultimo_modo = 1;
    
    oled_msg_t msg_inicial = {.frecuencia = frecuencia_actual, .modo = modo_onda, .paso = paso_frecuencia};
    xQueueOverwrite(oled_queue, &msg_inicial);

    while (1) {
        esp_task_wdt_reset(); // Reportar estado saludable al watchdog

        // Bloqueo eficiente por notificación desde la ISR del encoder
        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
        if (notified > 0) {
            oled_msg_t msg = {.frecuencia = frecuencia_actual, .modo = modo_onda, .paso = paso_frecuencia};
            xQueueOverwrite(oled_queue, &msg);
            guardar_estado_nvs(); // Guarda cambios del giro de perilla
        }

        // --- Monitorear Botón Modo (GPIO 4) ---
        int ext_btn = gpio_get_level(PIN_BOTON_MODO);
        if (ultimo_modo == 1 && ext_btn == 0) {
            vTaskDelay(pdMS_TO_TICKS(40)); 
            if (gpio_get_level(PIN_BOTON_MODO) == 0) {
                modo_onda = (modo_onda + 1) % 3;
                oled_msg_t msg = {.frecuencia = frecuencia_actual, .modo = modo_onda, .paso = paso_frecuencia};
                xQueueOverwrite(oled_queue, &msg);
                guardar_estado_nvs(); // Guarda cambio de forma de onda
            }
        }
        ultimo_modo = ext_btn;

        // --- Monitorear Botón Paso (GPIO 19) ---
        int enc_btn = gpio_get_level(PIN_ENC_SW);
        if (ultimo_sw == 1 && enc_btn == 0) {
            vTaskDelay(pdMS_TO_TICKS(40)); 
            if (gpio_get_level(PIN_ENC_SW) == 0) {
                if (paso_frecuencia == 1)        paso_frecuencia = 10;
                else if (paso_frecuencia == 10)  paso_frecuencia = 100;
                else if (paso_frecuencia == 100) paso_frecuencia = 1000;
                else                             paso_frecuencia = 1;
                
                oled_msg_t msg = {.frecuencia = frecuencia_actual, .modo = modo_onda, .paso = paso_frecuencia};
                xQueueOverwrite(oled_queue, &msg);
                guardar_estado_nvs(); // Guarda cambio del paso de ajuste
            }
        }
        ultimo_sw = enc_btn;
    }
}

// TAREA 4:
void vTaskOLED_Display(void *pvParameters) {
    esp_task_wdt_add(NULL); // Registrar en el Watchdog anti-fallos

    oled_msg_t msg_rx;
    char str_buffer[20];

    oled_inicializar();
    oled_limpiar();
    oled_escribir_texto(0, 10, "== RTOS DDS GEN ==");
    oled_escribir_texto(2, 0, "ONDA: ");
    oled_escribir_texto(4, 0, "FREQ: ");
    oled_escribir_texto(6, 0, "PASO: +/-");

    while (1) {
        esp_task_wdt_reset(); // Reportar estado saludable al watchdog

        // Espera un cambio con un timeout máximo de 1 segundo para no trabar el Watchdog
        if (xQueueReceive(oled_queue, &msg_rx, pdMS_TO_TICKS(1000)) == pdTRUE) {
            oled_escribir_texto(2, 42, "          ");
            oled_escribir_texto(4, 42, "          ");
            oled_escribir_texto(6, 60, "          ");

            const char *wave_name = (msg_rx.modo == 0) ? "SENOIDAL" : (msg_rx.modo == 1) ? "CUADRADA" : "TRIANGULO";
            oled_escribir_texto(2, 42, wave_name);

            snprintf(str_buffer, sizeof(str_buffer), "%ld Hz", msg_rx.frecuencia);
            oled_escribir_texto(4, 42, str_buffer);

            snprintf(str_buffer, sizeof(str_buffer), "%ld Hz", msg_rx.paso);
            oled_escribir_texto(6, 60, str_buffer);
        }
    }
}

// --- CONFIGURACIÓN DE PERIFÉRICOS (app_main) ---
void app_main(void) {
    // A. Inicializar Memoria No Volátil (NVS) para Persistencia
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // B. Cargar última configuración grabada
    cargar_estado_nvs();

    // C. Configurar e inicializar Task Watchdog Timer (TWDT) a 3 segundos de tolerancia
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 3000,
        .idle_core_mask = (1 << 0) | (1 << 1), 
        .trigger_panic = true // Reinicia el chip si hay un bloqueo permanente
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdt_config));

    // D. Carga matemática de tablas DDS según la frecuencia recuperada
    for (int i = 0; i < POINTS_TABLE; i++) {
        float angulo = (2.0f * M_PI * i) / (float)POINTS_TABLE;
        tabla_seno[i] = preparar_trama_mcp4921((uint16_t)(((sinf(angulo) + 1.0f) * 2047.0f)));
        tabla_cuadrada[i] = preparar_trama_mcp4921((i < POINTS_TABLE / 2) ? 4095 : 0);
        
        uint16_t val_tri = (i < POINTS_TABLE / 2) ? (uint16_t)((i * 4095) / (POINTS_TABLE / 2)) : (uint16_t)(4095 - (((i - POINTS_TABLE / 2) * 4095) / (POINTS_TABLE / 2)));
        tabla_triangular[i] = preparar_trama_mcp4921(val_tri);
    }
    incremento_fase = calcular_incremento_fase(frecuencia_actual);

    // E. Inicializar Colas RTOS
    oled_queue = xQueueCreate(1, sizeof(oled_msg_t)); 
    uart_rx_queue = xQueueCreate(5, 64 * sizeof(char));

    // F. Entradas Digitales e Interrupción por hardware del Encoder
    gpio_config_t enc_cfg = { 
        .pin_bit_mask = (1ULL << PIN_ENC_CLK) | (1ULL << PIN_ENC_DT), 
        .mode = GPIO_MODE_INPUT, 
        .pull_up_en = GPIO_PULLUP_ENABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_NEGEDGE 
    };
    ESP_ERROR_CHECK(gpio_config(&enc_cfg)); 

    gpio_config_t botones_cfg = { 
        .pin_bit_mask = (1ULL << PIN_ENC_SW) | (1ULL << PIN_BOTON_MODO), 
        .mode = GPIO_MODE_INPUT, 
        .pull_up_en = GPIO_PULLUP_ENABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, 
        .intr_type = GPIO_INTR_DISABLE 
    };
    ESP_ERROR_CHECK(gpio_config(&botones_cfg));

    esp_err_t err_isr = gpio_install_isr_service(0);
    if (err_isr != ESP_OK && err_isr != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Fallo al inicializar servicio ISR");
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_ENC_CLK, encoder_gpio_isr_handler, NULL));

    // G. Inicializar Conductor UART0
    uart_config_t uart_config = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, PIN_UART_TX, PIN_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // H. Inicializar Conductor I2C Maestro para OLED (v6.0)
    i2c_master_bus_config_t i2c_bus_config = { .clk_source = I2C_CLK_SRC_DEFAULT, .i2c_port = I2C_NUM_0, .scl_io_num = PIN_I2C_SCL, .sda_io_num = PIN_I2C_SDA, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &bus_handle));
    i2c_device_config_t dev_config = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = OLED_I2C_ADDR, .scl_speed_hz = 400000 };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &oled_dev_handle));

    // I. Inicializar Conductor SPI para DAC MCP4921
    spi_bus_config_t spi_cfg = { .miso_io_num = -1, .mosi_io_num = PIN_SPI_MOSI, .sclk_io_num = PIN_SPI_CLK, .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4 };
    spi_device_interface_config_t d_cfg = { .clock_speed_hz = 5000000, .mode = 0, .spics_io_num = PIN_SPI_CS, .queue_size = 1, .flags = SPI_DEVICE_NO_DUMMY };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &spi_cfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &d_cfg, &spi_dac_handle));

    // J. Inicializar Alarma GPTimer a 25 kHz (Muestreo determinista fuera de FreeRTOS)
    gptimer_handle_t dac_timer = NULL;
    gptimer_config_t t_cfg = { .clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP, .resolution_hz = 1000000 };
    ESP_ERROR_CHECK(gptimer_new_timer(&t_cfg, &dac_timer));
    gptimer_alarm_config_t al_cfg = { .reload_count = 0, .alarm_count = 40, .flags = { .auto_reload_on_alarm = true } };
    gptimer_event_callbacks_t cbs = {.on_alarm = dac_timer_isr_callback};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(dac_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(dac_timer, &al_cfg));
    ESP_ERROR_CHECK(gptimer_enable(dac_timer));
    ESP_ERROR_CHECK(gptimer_start(dac_timer));

    // K. Lanzamiento de Tareas Modulares
    xTaskCreate(vTaskUART,      "UART_RX_Task",   3072, NULL, 3, NULL);
    xTaskCreate(vTaskAnalizador, "Parser_Task",    3072, NULL, 2, NULL);
    xTaskCreate(vTaskInputMonitor,  "Inputs_Task",    2048, NULL, 2, &xInputsTaskHandle); 
    xTaskCreate(vTaskOLED_Display,  "OLED_Task",      3072, NULL, 1, NULL);

    ESP_LOGI(TAG, ">> Núcleo Digital Cerrado Exitosamente.");
}