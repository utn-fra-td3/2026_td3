#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "oled_driver.h"
#include "mux_driver.h"
#include "encoder_driver.h"

#define LED_BUILTIN     2
#define I2C_NUM         I2C_NUM_0
#define OLED_ADDR       0x3C

// ── Pines SPI / DAC ────────────────────────────────────────────────────────
#define PIN_MOSI        23
#define PIN_CLK         18
#define PIN_CS          5

// ── Pines de control ───────────────────────────────────────────────────────
#define BUTTON_WAVE_IO  27
#define PWM_SQUARE_IO   26

// ── DDS / Señal ────────────────────────────────────────────────────────────
#define LUT_SIZE        1024
#define FREQ_MIN        100
#define FREQ_MAX        20000
#define DAC_MAX         4095

// ── UART2 ──────────────────────────────────────────────────────────────────
#define UART_NUM        UART_NUM_2
#define UART_TX_PIN     17
#define UART_RX_PIN     16
#define BUF_SIZE        256

typedef enum { WAVE_SINE = 0, WAVE_TRIANGLE = 1, WAVE_SQUARE = 2 } wave_type_t;

// ── Configuración LEDC (PWM) ───────────────────────────────────────────────
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_8_BIT

// ── Estructura de Eventos Unificada ────────────────────────────────────────
typedef enum {
    CMD_ENC_UP,
    CMD_ENC_DOWN,
    CMD_ENC_BTN,
    CMD_BTN_WAVE,
    CMD_UART_FREQ,
    CMD_UART_WAVE
} unified_cmd_t;

typedef struct {
    unified_cmd_t cmd;
    int value;
} unified_msg_t;

// ── Estructura del Estado Global Protegido ──────────────────────────────────
typedef struct {
    int freq_hz;
    int freq_step;
    wave_type_t current_wave;
    float amp_v;
    float offset_v;
} system_state_t;


// Sincronización y Comunicación
QueueHandle_t control_queue = NULL;  // Cola de eventos - Control
QueueHandle_t state_queue   = NULL; // Cola estado GLOBAL

static SemaphoreHandle_t sem_hw = NULL;
static SemaphoreHandle_t sem_nvs = NULL;

// Variables DDS de alta frecuencia
static uint16_t lut_sine[LUT_SIZE];
static uint16_t lut_triangle[LUT_SIZE];
static uint16_t *active_lut;
static volatile uint32_t dds_phase_per_cycle = 0;
static spi_device_handle_t spi;
volatile bool nvs_saved_flag = false;

// ──────────────────────────────────────────────────────────────────────────
// Generación de Tablas LUT
// ──────────────────────────────────────────────────────────────────────────
static void build_luts(void) {
    for (int i = 0; i < LUT_SIZE; i++) {
        float rad = (2.0f * M_PI * i) / LUT_SIZE;
        uint16_t sv = (uint16_t)((sinf(rad) + 1.0f) * 0.5f * DAC_MAX);
        lut_sine[i] = 0x3000 | (sv & 0x0FFF);

        float tri = (i < LUT_SIZE / 2)
                    ? (float)i / (LUT_SIZE / 2)
                    : 2.0f - (float)i / (LUT_SIZE / 2);
        uint16_t tv = (uint16_t)(tri * DAC_MAX);
        lut_triangle[i] = 0x3000 | (tv & 0x0FFF);
    }
}

// ──────────────────────────────────────────────────────────────────────────
// SPI
// ──────────────────────────────────────────────────────────────────────────
static void spi_init(void) {
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_DISABLED);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
        .flags = SPI_DEVICE_NO_DUMMY,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
}

// ──────────────────────────────────────────────────────────────────────────
// PWM Hardware (LEDC)
// ──────────────────────────────────────────────────────────────────────────
static void square_pwm_init(void) {
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PWM_SQUARE_IO,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);
}

// ──────────────────────────────────────────────────────────────────────────
// ISR para Botón de Forma de Onda con Antirrebote por Hardware Timer
// ──────────────────────────────────────────────────────────────────────────
static void IRAM_ATTR button_wave_isr_handler(void *arg) {
    static uint64_t last_press_time = 0;
    uint64_t now = esp_timer_get_time();
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Filtro antirrebote temporal: 200 ms (200.000 µs)
    if ((now - last_press_time) > 200000ULL) {
        last_press_time = now;
        unified_msg_t msg = {
            .cmd = CMD_BTN_WAVE,
            .value = 0
        };
        xQueueSendFromISR(control_queue, &msg, &xHigherPriorityTaskWoken);
    }

    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

static void button_wave_init(void) {
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << BUTTON_WAVE_IO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE, // Interrupción por flanco de bajada
    };
    gpio_config(&btn_conf);

    gpio_isr_handler_add(BUTTON_WAVE_IO, button_wave_isr_handler, NULL);
}

// ──────────────────────────────────────────────────────────────────────────
// TAREA 1: Salida DDS (Bucle de rendimiento crítico)
// ──────────────────────────────────────────────────────────────────────────
static void spi_output_task(void *pvParameters) {
    spi_device_acquire_bus(spi, portMAX_DELAY);

    uint32_t phase_acc = 0;
    uint32_t last_ccount = esp_cpu_get_cycle_count();

    while (1) {
        uint32_t now = esp_cpu_get_cycle_count();
        uint32_t delta_cycles = now - last_ccount;
        last_ccount = now;

        phase_acc += delta_cycles * dds_phase_per_cycle;

        uint32_t idx = phase_acc >> 22;
        uint16_t cmd = active_lut[idx];

        spi_transaction_t t = {
            .length = 16,
            .flags = SPI_TRANS_USE_TXDATA,
        };
        t.tx_data[0] = (cmd >> 8) & 0xFF;
        t.tx_data[1] = cmd & 0xFF;

        spi_device_polling_start(spi, &t, portMAX_DELAY);
        spi_device_polling_end(spi, portMAX_DELAY);
    }
}

// ──────────────────────────────────────────────────────────────────────────
// TAREA 2: Control de Hardware (Configura PWM, MUX y Tabla LUT)
// ──────────────────────────────────────────────────────────────────────────
static void hw_signal_task(void *pvParameters) {
    uint64_t cpu_freq_hz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000ULL;
    system_state_t st;

    while (1) {
        // Espera el semáforo
        if (xSemaphoreTake(sem_hw, portMAX_DELAY) == pdTRUE) {

            // Lee el estado de la ÚNICA cola sin borrarlo
            xQueuePeek(state_queue, &st, portMAX_DELAY);

            // 1. Conmutar MUX y Tabla LUT
            if (st.current_wave == WAVE_SINE) {
                active_lut = lut_sine;
                mux_select(MUX_CH_SINE);
            } else if (st.current_wave == WAVE_TRIANGLE) {
                active_lut = lut_triangle;
                mux_select(MUX_CH_TRIANGLE);
            } else {
                mux_select(MUX_CH_SQUARE);
            }

            // 2. Cálculo del paso de fase DDS
            dds_phase_per_cycle = (uint32_t)(((uint64_t)st.freq_hz * 4294967296ULL) / cpu_freq_hz);

            // 3. Ajuste de Timer y Duty de PWM
            ledc_timer_bit_t res = (st.freq_hz > 10000) ? LEDC_TIMER_7_BIT : LEDC_TIMER_8_BIT;
            ledc_timer_config_t timer_cfg = {
                .speed_mode = LEDC_MODE,
                .timer_num = LEDC_TIMER,
                .duty_resolution = res,
                .freq_hz = st.freq_hz,
                .clk_cfg = LEDC_AUTO_CLK
            };
            ledc_timer_config(&timer_cfg);

            if (st.current_wave == WAVE_SQUARE) {
                ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, (res == LEDC_TIMER_8_BIT) ? 127 : 63);
            } else {
                ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
            }
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────
// TAREA 3: Lógica del Sistema (Event-Loop Central)
// ──────────────────────────────────────────────────────────────────────────
static void sys_logic_task(void *pvParameters) {
    unified_msg_t msg;
    system_state_t st;
    int save_timeout = 0;
    bool pending_save = false;
    char uart_notify[48];

    while (1) {
        if (xQueueReceive(control_queue, &msg, pdMS_TO_TICKS(50)) == pdTRUE) {
            bool signal_changed = false;

            // Leer estado actual de la cola
            xQueuePeek(state_queue, &st, portMAX_DELAY);

            switch (msg.cmd) {
                case CMD_ENC_UP:
                    st.freq_hz += st.freq_step;
                    if (st.freq_hz > FREQ_MAX) st.freq_hz = FREQ_MAX;
                    signal_changed = true;
                    break;

                case CMD_ENC_DOWN:
                    st.freq_hz -= st.freq_step;
                    if (st.freq_hz < FREQ_MIN) st.freq_hz = FREQ_MIN;
                    signal_changed = true;
                    break;

                case CMD_ENC_BTN:
                    st.freq_step *= 10;
                    if (st.freq_step > 1000) st.freq_step = 1;
                    snprintf(uart_notify, sizeof(uart_notify), "STATUS:STEP=%d\r\n", st.freq_step);
                    uart_write_bytes(UART_NUM, uart_notify, strlen(uart_notify));
                    break;

                case CMD_BTN_WAVE:
                    st.current_wave = (st.current_wave + 1) % 3;
                    signal_changed = true;
                    break;

                case CMD_UART_FREQ:
                    st.freq_hz = msg.value;
                    signal_changed = true;
                    break;

                case CMD_UART_WAVE:
                    st.current_wave = (wave_type_t)msg.value;
                    signal_changed = true;
                    break;
            }

            // Actualizar la ÚNICA cola de estado
            xQueueOverwrite(state_queue, &st);

            if (msg.cmd == CMD_ENC_UP || msg.cmd == CMD_ENC_DOWN) {
                snprintf(uart_notify, sizeof(uart_notify), "STATUS:FREQ=%d\r\n", st.freq_hz);
                uart_write_bytes(UART_NUM, uart_notify, strlen(uart_notify));
            } else if (msg.cmd == CMD_BTN_WAVE) {
                snprintf(uart_notify, sizeof(uart_notify), "STATUS:WAVE=%d\r\n", (int)st.current_wave);
                uart_write_bytes(UART_NUM, uart_notify, strlen(uart_notify));
            }

            pending_save = true;
            save_timeout = 20;

            if (signal_changed) {
                xSemaphoreGive(sem_hw); // <-- Semáforo en vez de notificación
            }
        }

        // Debounce de guardado NVS
        if (pending_save) {
            if (save_timeout > 0) {
                save_timeout--;
            } else {
                pending_save = false;
                xSemaphoreGive(sem_nvs); // <-- Semáforo en vez de notificación
            }
        }
    }
}
// ──────────────────────────────────────────────────────────────────────────
// TAREA 4: Almacenamiento en NVS Flash (Operación Pesada en Background)
// ──────────────────────────────────────────────────────────────────────────

static void nvs_storage_task(void *pvParameters) {
    int f = 1000, s = 100;
    int32_t w = 0;
    nvs_handle_t my_handle;
    char nvs_msg[64]; // <-- Buffer para el mensaje UART hacia la Raspberry Pi
    system_state_t st;

    while (1) {
        if(xSemaphoreTake(sem_nvs, portMAX_DELAY) == pdTRUE){
            xQueuePeek(state_queue, &st, portMAX_DELAY);

            f = st.freq_hz;
            s = st.freq_step;
            w = (int32_t)st.current_wave;

            if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
                nvs_set_i32(my_handle, "freq", f);
                nvs_set_i32(my_handle, "wave", w);
                nvs_set_i32(my_handle, "step", s);
                nvs_commit(my_handle);
                nvs_close(my_handle);
                nvs_saved_flag = true;
                printf("NVS: Guardado completado (%d Hz, Onda %ld)\n", f, (long)w);

                // ── Notificar a la Raspberry Pi que se guardó el estado en NVS ──
                snprintf(nvs_msg, sizeof(nvs_msg), "STATUS:NVS_SAVED:%d,%ld,%d\r\n", f, (long)w, s);
                uart_write_bytes(UART_NUM, nvs_msg, strlen(nvs_msg));
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────
// TAREA 5: Lectura de Entradas Analógicas (ADC)
// ──────────────────────────────────────────────────────────────────────────
static void input_handler_task(void *pvParameters) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
    adc1_config_channel_atten(ADC1_CHANNEL_7, ADC_ATTEN_DB_11);
    system_state_t st;

    while (1) {
        int raw_amp = adc1_get_raw(ADC1_CHANNEL_6);
        int raw_off = adc1_get_raw(ADC1_CHANNEL_7);

        float a = ((float)raw_amp / 4095.0f) * 5.0f;
        float o = (((float)raw_off / 4095.0f) * 5.0f) - 2.5f;

        xQueuePeek(state_queue, &st, portMAX_DELAY);
        
        st.amp_v = a;
        st.offset_v = o;

        xQueueOverwrite(state_queue, &st);

        vTaskDelay(pdMS_TO_TICKS(100)); // 10 Hz
    }
}

// ──────────────────────────────────────────────────────────────────────────
// TAREA 6: Pantalla OLED (UI)
// ──────────────────────────────────────────────────────────────────────────
static void ui_display_task(void *pvParameters) {
    oled_driver_init();
    vTaskDelay(pdMS_TO_TICKS(500));
    ssd1306_clear();
    system_state_t local_state;
    char buf_left[16];
    char buf_right[16];
    int nvs_timer = -1;

    int last_freq = -1;
    int last_step = -1;
    wave_type_t last_wave = -1;

    while (1) {
        uint8_t ping_data[2] = {0x00, 0xE3};
        esp_err_t oled_status = i2c_master_write_to_device(I2C_NUM, OLED_ADDR, ping_data, 2, pdMS_TO_TICKS(10));
        xQueuePeek(state_queue, &local_state, portMAX_DELAY);

        if (oled_status != ESP_OK) {
            gpio_set_level(LED_BUILTIN, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        } else {
            gpio_set_level(LED_BUILTIN, 0);
        }

        // Actualizar forma de onda
        if (local_state.current_wave != last_wave) {
            last_wave = local_state.current_wave;
            if (last_wave == WAVE_SINE) snprintf(buf_left, sizeof(buf_left), "W:SINE ");
            else if (last_wave == WAVE_TRIANGLE) snprintf(buf_left, sizeof(buf_left), "W:TRI  ");
            else snprintf(buf_left, sizeof(buf_left), "W:SQR  ");
            oled_write_grid(0, 0, buf_left);
        }

        // Indicador de NVS
        if (nvs_saved_flag) {
            nvs_saved_flag = false;
            nvs_timer = 10;
            oled_write_grid(1, 0, "[*] MEM ");
        }

        if (nvs_timer > 0) {
            nvs_timer--;
        } else if (nvs_timer == 0) {
            oled_write_grid(1, 0, "        ");
            nvs_timer = -1;
        }

        // Actualizar Frecuencia y Paso
        if (local_state.freq_hz != last_freq || local_state.freq_step != last_step) {
            last_freq = local_state.freq_hz;
            last_step = local_state.freq_step;
            snprintf(buf_left, sizeof(buf_left), "F:%d  ", local_state.freq_hz);
            oled_write_grid(0, 1, buf_left);
            snprintf(buf_right, sizeof(buf_right), "P:%d  ", local_state.freq_step);
            oled_write_grid(1, 1, buf_right);
        }

        // Actualizar Amplitud y Offset
        snprintf(buf_left, sizeof(buf_left), "A:%.1fV  ", local_state.amp_v);
        oled_write_grid(0, 2, buf_left);

        snprintf(buf_right, sizeof(buf_right), "O:%+.1fV  ", local_state.offset_v);
        oled_write_grid(1, 2, buf_right);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ──────────────────────────────────────────────────────────────────────────
// TAREA 7: Comando Serial (UART)
// ──────────────────────────────────────────────────────────────────────────
static void uart_cmd_task(void *pvParameters) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    system_state_t st;

    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    gpio_set_pull_mode(UART_RX_PIN, GPIO_PULLUP_ONLY);

    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
    char resp[64];
    unified_msg_t msg;

    printf("[UART] Tarea iniciada y escuchando en RX=%d, TX=%d\n", UART_RX_PIN, UART_TX_PIN);

    while (1) {
        int len = uart_read_bytes(UART_NUM, data, BUF_SIZE - 1, pdMS_TO_TICKS(50));
        if (len > 0) {
            data[len] = '\0';
            printf("[UART RX] Llego (%d bytes): %s\n", len, (char *)data);

            if (strstr((char *)data, "FREQ=") != NULL) {
                char *p = strstr((char *)data, "FREQ=");
                int new_freq = atoi(p + 5);
                if (new_freq >= FREQ_MIN && new_freq <= FREQ_MAX) {
                    msg.cmd = CMD_UART_FREQ;
                    msg.value = new_freq;
                    xQueueSend(control_queue, &msg, portMAX_DELAY);
                    snprintf(resp, sizeof(resp), "OK: FREQ = %d Hz\r\n", new_freq);
                } else {
                    snprintf(resp, sizeof(resp), "ERROR: FUERA DE RANGO\r\n");
                }
                uart_write_bytes(UART_NUM, resp, strlen(resp));
                printf("[UART TX -> RPi] %s", resp);

            } else if (strstr((char *)data, "WAVE=") != NULL) {
                char *p = strstr((char *)data, "WAVE=");
                int w = atoi(p + 5);
                if (w >= 0 && w <= 2) {
                    msg.cmd = CMD_UART_WAVE;
                    msg.value = w;
                    xQueueSend(control_queue, &msg, portMAX_DELAY);
                    snprintf(resp, sizeof(resp), "OK: WAVE = %d\r\n", w);
                } else {
                    snprintf(resp, sizeof(resp), "ERROR: ONDA INVALIDA\r\n");
                }
                uart_write_bytes(UART_NUM, resp, strlen(resp));
                printf("[UART TX -> RPi] %s", resp);

            } else if (strstr((char *)data, "SYNC") != NULL) {
                int cur_f = 1000, cur_s = 100;
                wave_type_t cur_w = WAVE_SINE;

                xQueuePeek(state_queue, &st, portMAX_DELAY); 
                    cur_f = st.freq_hz;
                    cur_w = st.current_wave;
                    cur_s = st.freq_step;

                snprintf(resp, sizeof(resp), "STATUS:NVS_SAVED:%d,%d,%d\r\n", cur_f, (int)cur_w, cur_s);
                uart_write_bytes(UART_NUM, resp, strlen(resp));
                printf("[UART TX -> RPi] %s", resp);
            }
        }
    }
}
// ──────────────────────────────────────────────────────────────────────────
// app_main
// ──────────────────────────────────────────────────────────────────────────
void app_main(void) {
    // ── 1. NVS Y WATCHDOG ──
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Cargar estado inicial desde NVS
    system_state_t init_state = {
        .freq_hz = 1000, .freq_step = 100, .current_wave = WAVE_SINE, .amp_v = 0.0f, .offset_v = 0.0f
    };

    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        int32_t saved_freq = 1000;
        int32_t saved_wave = 0;
        int32_t saved_step = 100;

        if (nvs_get_i32(my_handle, "freq", &saved_freq) == ESP_OK) init_state.freq_hz = saved_freq;
        if (nvs_get_i32(my_handle, "wave", &saved_wave) == ESP_OK) init_state.current_wave = (wave_type_t)saved_wave;
        if (nvs_get_i32(my_handle, "step", &saved_step) == ESP_OK) init_state.freq_step = saved_step;
        nvs_close(my_handle);
    }

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 3000,
        .idle_core_mask = 0,
        .trigger_panic = false
    };
    esp_task_wdt_reconfigure(&wdt_config);

    // ── 2. RECURSOS DE SINCRONIZACIÓN ──
    state_queue   = xQueueCreate(1, sizeof(system_state_t)); // Cola única de estado (Mailbox)
    sem_hw        = xSemaphoreCreateBinary();
    sem_nvs       = xSemaphoreCreateBinary();
    control_queue = xQueueCreate(15, sizeof(unified_msg_t));

    if (state_queue == NULL || control_queue == NULL) {
        printf("Error fatal: No se pudieron crear los recursos RTOS\n");
    }

    xQueueOverwrite(state_queue, &init_state);

    // ── 3. INICIALIZACIÓN DE HARDWARE ──
    gpio_reset_pin(LED_BUILTIN);
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_BUILTIN, 0);

    build_luts();
    active_lut = lut_sine;

    spi_init();
    encoder_init();
    button_wave_init();
    square_pwm_init();
    mux_init();

    // ── 4. CREACIÓN DE TAREAS ──
    xTaskCreate(spi_output_task,  "dds",      4096, NULL, 1, NULL);
    xTaskCreate(nvs_storage_task, "nvs_task", 3072, NULL, 1, NULL);
    xTaskCreate(ui_display_task,  "ui",       4096, NULL, 2, NULL);
    xTaskCreate(uart_cmd_task,    "uart",     4096, NULL, 2, NULL);
    xTaskCreate(input_handler_task,"input_adc",3072, NULL, 2, NULL);
    xTaskCreate(hw_signal_task,   "hw_sig",   4096, NULL, 3, NULL);
    xTaskCreate(sys_logic_task,   "sys_logic",4096, NULL, 3, NULL);

    // Disparar la primera actualización de hardware
    xSemaphoreGive(sem_hw);
}