/*
 * main.c — Ecualizador de audio de 3 bandas en ESP32
 *
 * Pipeline de audio:
 *   ISR on_recv → notifica guardiana → lee ADC, escribe DAC, rota buffers
 *                                    → envía puntero al DSP
 *   DSP          → aplica 3 filtros FIR + ganancia → sobrescribe buffer
 *
 * Triple buffer: los 3 slots rotan entre roles RX → DSP → TX → RX
 *
 * Distribución de cores:
 *   Core 0 (tiempo real): guardian_task (alta prioridad), dsp_task (media)
 *   Core 1 (control):     uart_task, encoder_task, flash_task, lcd_task
 *
 * Hardware:
 *   ADC: PCM1808  (I2S slave, 24-bit en frame de 32)
 *   DAC: PCM5102  (I2S slave, 24-bit en frame de 32)
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_dsp.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_err.h"

<<<<<<< Updated upstream
static const char *TAG = "equalizer";
=======
#include "driver/pulse_cnt.h"

static pcnt_unit_handle_t encoder_pcnt_unit = NULL; //handle del contador de pulsos

static const char *TAG = "equalizer"; //estiqueta para NVS
>>>>>>> Stashed changes

// =============================================================================
// Estructura de parámetros del ecualizador
// =============================================================================
// Agrupa todo el estado configurable en un único mensaje de cola.
// Se distribuye por param_config_queue (profundidad 1):
//   Escritores: encoder_task, uart_task  → xQueueOverwrite
//   Lectores:   dsp_task, flash_task, lcd_task → xQueuePeek (no consumen)

typedef struct {
    float gain_db_low;    // Ganancia banda baja  [GAIN_DB_MIN, GAIN_DB_MAX]
    float gain_db_mid;    // Ganancia banda media
    float gain_db_high;   // Ganancia banda alta
    int   selected_band;  // Banda activa en el encoder: 0=low, 1=mid, 2=high
} eq_params_t;

// =============================================================================
// Configuración de pines y parámetros de audio
// =============================================================================

// I2S — PCM1808 (ADC) y PCM5102 (DAC) comparten BCLK y LRCK

#define I2S_MCLK_GPIO   GPIO_NUM_3   // → PCM1808 SCKI (via 33 ohm recomendado para integridad de señal)
#define I2S_BCLK_GPIO   GPIO_NUM_26   // → PCM1808 BCK  y PCM5102 BCK (via 33 ohm recomendado para integridad de señal)
#define I2S_LRCK_GPIO   GPIO_NUM_25   // → PCM1808 LRCK y PCM5102 LCK (via 33 ohm recomendado para integridad de señal)
#define I2S_DIN_GPIO    GPIO_NUM_34   // ← PCM1808 DOUT  (GPIO34: input only pero válido para DIN a ESP32)
#define I2S_DOUT_GPIO   GPIO_NUM_23   // → PCM5102 DIN

#define SAMPLE_RATE      44100        // 44100 Hz es estándar para audio, PCM1808 lo soporta nativamente.
#define FRAMES_PER_BUF   256          // frames estéreo por buffer (ajustar según latencia deseada y uso de CPU)
#define CHANNELS         2            // estéreo: 2 canales (L y R)
// Total de int32_t por buffer: 256 frames × 2 canales = 1024 muestras = 4096 bytes
#define SAMPLES_PER_BUF  (FRAMES_PER_BUF * CHANNELS)

// UART
//#define UART_PORT        UART_NUM_1
#define UART_PORT        UART_NUM_2
#define UART_TX_GPIO     GPIO_NUM_17  // Tx del ESP32 → RX del dispositivo de control (USB-UART)
#define UART_RX_GPIO     GPIO_NUM_16  // Rx del ESP32 → TX del dispositivo de control (USB-UART)
#define UART_BUF_SIZE    256
#define UART_EVENT_QUEUE_LEN  10      // cola de eventos nativa del driver UART (creada por uart_driver_install)
#define UART_MSG_MAX_LEN      32      // tamaño máximo de un comando de texto

// Encoder rotativo tipo Gray (DT-CLK-SW = B-A-SW)
#define ENCODER_CLK      GPIO_NUM_33   // CLK = señal A del encoder Gray
#define ENCODER_DT       GPIO_NUM_32   // DT  = señal B del encoder Gray
#define ENCODER_SW       GPIO_NUM_27   // SW: pulsador (activo en bajo, pull-up interno)

// I2C / LCD
#define I2C_SCL_GPIO     GPIO_NUM_18  // SCL del I2C para LCD
#define I2C_SDA_GPIO     GPIO_NUM_21  // SDA del I2C para LCD
#define I2C_FREQ_HZ      100000       // 100 kHz es suficiente para un LCD, no es necesario más rápido

#define LCD_I2C_ADDR     0x27        // PCF8574A: dirección de fábrica con A0=A1=A2=1 (pull-up, sin jumpers)
#define LCD_COLS         20          // LCD2004: 20 columnas
#define LCD_ROWS         4           // LCD2004: 4 filas

// NVS
#define NVS_NAMESPACE    "equalizer" // Namespace para las claves de ganancia
#define NVS_KEY_LOW      "gain_low"  
#define NVS_KEY_MID      "gain_mid"
#define NVS_KEY_HIGH     "gain_high"

// Filtros FIR — siguiendo la nota de aplicación de Analog Devices (Smith,
// "The Scientist and Engineer's Guide to DSP", cap. 16): el kernel debe
// tener M+1 puntos con M PAR, para que el centro de simetría i=M/2 sea un
// índice entero exacto. FIR_TAPS = M+1 = 33 (M=32, par) — antes estaba en
// 32 puntos (M=31, impar), lo que descentraba el sinc y la ventana de
// Hamming respecto al punto medio real, rompiendo la simetría del filtro.
#define FIR_M            32              // debe ser par
#define FIR_TAPS         (FIR_M + 1)     // 33 puntos — número impar, centro en M/2

// Límites de ganancia: ±24 dB 
#define GAIN_DB_MIN     -24.0f
#define GAIN_DB_MAX      24.0f
#define GAIN_DB_DEFAULT   0.0f
#define MASTER_GAIN   -12.0f            //ganancia general de salida, para evitar saturación del DAC

// Tamaños de stack
#define STACK_GUARDIAN   (4096 + SAMPLES_PER_BUF * 4)  // buffer local de arranque y rotación (int32_t) + margen para la lógica de la tarea
#define STACK_DSP        8192   // líneas de retardo FIR en stack local
#define STACK_AUX        4096   // stack tareas auxiliares

// =============================================================================
// Handles globales
// =============================================================================

static i2s_chan_handle_t i2s_tx_handle;     // canal I2S para el DAC (salida)
static i2s_chan_handle_t i2s_rx_handle;     // canal I2S para el ADC (entrada)

static TaskHandle_t     guardian_handle     = NULL;
static QueueHandle_t    dsp_queue;          // profundidad 1: puntero al buffer más fresco
static QueueHandle_t    param_config_queue; // profundidad 1: eq_params_t compartido por todas las tareas
static QueueHandle_t    uart_event_queue;   // cola nativa del driver UART (eventos del hardware, vía ISR interna)
static SemaphoreHandle_t change_semaphore;  // semáforo binario: lo dan uart_task/encoder_task al modificar ganancia
static SemaphoreHandle_t dsp_done_sem;      // semáforo binario: lo da dsp

// =============================================================================
// Triple buffer
// =============================================================================

typedef int32_t audio_buffer_t[SAMPLES_PER_BUF]; // buffer de audio: array de int32_t con muestras intercaladas L y R

// Los 3 slots físicos. Alineados a 16 bytes para operaciones SIMD del Xtensa.
static audio_buffer_t __attribute__((aligned(16))) audio_buffers[3];

// Roles como punteros directos — modificados SOLO por guardian_task y DSP_task
// Rotar 3 punteros es más rápido que copiar en variables auxiliares
static audio_buffer_t *buf_rx  = &audio_buffers[0];   // slot que el ADC está llenando ahora
static audio_buffer_t *buf_dsp = &audio_buffers[1];   // slot que el DSP está procesando ahora
static audio_buffer_t *buf_tx  = &audio_buffers[2];   // slot que el DAC está vaciando ahora

// =============================================================================
// Parámetros del ecualizador — valor por defecto
// =============================================================================
// Solo se usa para inicializar param_config_queue en app_main.
// El resto del sistema lee y escribe exclusivamente a través de la cola.

static const eq_params_t EQ_DEFAULTS = {
    .gain_db_low  = GAIN_DB_DEFAULT,
    .gain_db_mid  = GAIN_DB_DEFAULT,
    .gain_db_high = GAIN_DB_DEFAULT,
    .selected_band = 0,
};

static inline float db_to_linear(float db)
{
    return powf(10.0f, db / 20.0f);     //10^(db/20) convierte dB a factor lineal
}

//irq DMA lleno del i2s
static bool IRAM_ATTR i2s_rx_done_cb(i2s_chan_handle_t  handle,
                                      i2s_event_data_t  *event,
                                      void              *user_ctx)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(guardian_handle, &woken);    //notifica a guardiana
    return woken == pdTRUE;
}


// =============================================================================
// Inicialización I2S full-duplex
// =============================================================================

static esp_err_t i2s_init_full_duplex(void)
{
    // Un único i2s_new_channel con ambos handles crea el par vinculado.
    // Full-duplex: TX y RX comparten el mismo periférico y sus clocks.
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 3;              // un descriptor por slot del triple buffer
    chan_cfg.dma_frame_num = FRAMES_PER_BUF; // frames por descriptor DMA

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, &i2s_rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src        = I2S_CLK_SRC_APLL,          // APLL: mejor precisión de audio usa PLL dedicado
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256,     // MCLK = 256 × 44100 ≈ 11.29 MHz
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT,         // frame 32-bit, 24 útiles (PCM1808/5102)
                        I2S_SLOT_MODE_STEREO),            //audio estéreo
        .gpio_cfg = {
            .mclk = I2S_MCLK_GPIO,
            .bclk = I2S_BCLK_GPIO,
            .ws   = I2S_LRCK_GPIO,
            .dout = I2S_DOUT_GPIO,
            .din  = I2S_DIN_GPIO,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };

    // Full-duplex requiere la misma configuración en ambos canales aunque DAC no use MCLK físicamente
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg));
    //callbacks
    i2s_event_callbacks_t cbs = {
        .on_recv       = i2s_rx_done_cb,
        .on_recv_q_ovf = NULL,
        .on_sent       = NULL,
        .on_send_q_ovf = NULL,
    };
    ESP_LOGI(TAG, "Registrando callback");
    esp_err_t err = i2s_channel_register_event_callback(i2s_rx_handle, &cbs, NULL);
    ESP_LOGI(TAG, "callback = %s", esp_err_to_name(err));

    ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(i2s_rx_handle));

    return ESP_OK;
}

// =============================================================================
// Filtros FIR — diseño y procesamiento con esp-dsp
// =============================================================================
//
// Se implementa dsps_fird_f32() para cada banda (baja, media, alta) y cada canal (L, R),
//
// Flujo por bloque:
//   int32_t estéreo intercalado  →  separo canales  →  convierto a float mono L / float mono R  →  3x dsps_fird_f32() por canal 
//   →  suma con ganancia aplicada  →  intercalo canales  →  convierto a int32_t 24-bit justificado a la izquierda


// ── Coeficientes (compartidos entre canales L y R en cada banda) ─────────────
static __attribute__((aligned(16))) float lp_coeffs[FIR_TAPS]; 
static __attribute__((aligned(16))) float bp_coeffs[FIR_TAPS];
static __attribute__((aligned(16))) float hp_coeffs[FIR_TAPS];

// ── Líneas de retardo: una por canal (L=0, R=1) por banda ────────────────────
// +4 de margen: requisito de alineación interno de dsps_fird_init_f32 para
// su camino SIMD, independiente de la cantidad de coeficientes del filtro.
static __attribute__((aligned(16))) float delay_lp[CHANNELS][FIR_TAPS + 4];
static __attribute__((aligned(16))) float delay_bp[CHANNELS][FIR_TAPS + 4];
static __attribute__((aligned(16))) float delay_hp[CHANNELS][FIR_TAPS + 4];

// ── Estructuras fir_f32_t de esp-dsp: una por canal por banda ────────────────
static fir_f32_t fir_lp[CHANNELS];
static fir_f32_t fir_bp[CHANNELS];
static fir_f32_t fir_hp[CHANNELS];

// ── Buffers de trabajo alineados — estáticos para no usar stack de la tarea ──
// Tamaño: FRAMES_PER_BUF muestras mono (la mitad del buffer estéreo intercalado)
static __attribute__((aligned(16))) float ch_in [CHANNELS][FRAMES_PER_BUF];
static __attribute__((aligned(16))) float out_lp[CHANNELS][FRAMES_PER_BUF];
static __attribute__((aligned(16))) float out_bp[CHANNELS][FRAMES_PER_BUF];
static __attribute__((aligned(16))) float out_hp[CHANNELS][FRAMES_PER_BUF];

// ── Pasa-bajos puro — Tabla 16-1 de la nota de Analog Devices ───────────────
// h[i] = sinc(2·fc·(i-M/2)) · ventana_Hamming[i],  normalizado a ganancia
// unitaria en DC (sum(h) = 1). fc es frecuencia normalizada [0, 0.5].
// Esta es la ÚNICA forma de "lowpass" que se calcula directo con sinc — el
// pasa-altos y el pasa-banda se derivan de éste por inversión espectral,
// nunca con una resta de sincos como hacía la versión anterior.
static void fir_design_lowpass(float *coeffs, float fc)
{
    int half = FIR_M / 2;   // FIR_M es par → división exacta, sin truncar

    for (int i = 0; i < FIR_TAPS; i++) {
        float x = (float)(i - half);
        if (x == 0.0f) {
            coeffs[i] = 2.0f * (float)M_PI * fc;
        } else {
            coeffs[i] = sinf(2.0f * (float)M_PI * fc * x) / x;
        }
        // Ventana de Hamming (Ecuación 16-1 del documento)
        float w = 0.54f - 0.46f * cosf(2.0f * (float)M_PI * i / FIR_M);
        coeffs[i] *= w;
    }

    // Normalizar para ganancia unitaria en DC — válido SOLO para pasa-bajos,
    // porque a f=0 todas las sinusoides del kernel valen 1.
    float sum = 0.0f;
    for (int i = 0; i < FIR_TAPS; i++) sum += coeffs[i];
    if (fabsf(sum) > 1e-9f) {
        float inv = 1.0f / sum;
        for (int i = 0; i < FIR_TAPS; i++) coeffs[i] *= inv;
    }
}

// ── Inversión espectral — convierte un pasa-bajos en pasa-altos ─────────────
// (Cap. 14 del documento, usado en la Tabla 16-2): negar todos los
// coeficientes y sumar 1 al del centro. Esto refleja la respuesta en
// frecuencia respecto a fs/4, transformando "todo lo de abajo pasa" en
// "todo lo de abajo se bloquea".
static void fir_spectral_invert(float *coeffs)
{
    for (int i = 0; i < FIR_TAPS; i++) coeffs[i] = -coeffs[i];
    coeffs[FIR_M / 2] += 1.0f;
}

// ── Pasa-banda — Tabla 16-2 exacta del documento ─────────────────────────────
// 1) lowpass con corte en fc_low   → A[]
// 2) lowpass con corte en fc_high  → B[], luego invertido espectralmente (pasa-altos)
// 3) H = A + B   → rechazo de banda (bloquea justo la banda fc_low..fc_high)
// 4) invertir H espectralmente     → pasa-banda (pasa exactamente fc_low..fc_high)
static void fir_design_bandpass(float *coeffs, float fc_low, float fc_high)
{
    float a[FIR_TAPS], b[FIR_TAPS];

    fir_design_lowpass(a, fc_low);    // A: pasa todo por debajo de fc_low
    fir_design_lowpass(b, fc_high);   // B: pasa todo por debajo de fc_high
    fir_spectral_invert(b);           // B invertido: pasa todo por ENCIMA de fc_high

    for (int i = 0; i < FIR_TAPS; i++) coeffs[i] = a[i] + b[i];  // rechazo de banda
    fir_spectral_invert(coeffs);                                  // → pasa-banda
}

// ── Pasa-altos — caso particular: lowpass + inversión espectral ─────────────
static void fir_design_highpass(float *coeffs, float fc)
{
    fir_design_lowpass(coeffs, fc);
    fir_spectral_invert(coeffs);
}

static void init_fir_bands(void)
{
    // Frecuencias normalizadas (fracción de SAMPLE_RATE, rango 0–0.5).
    // Un pasa-bajos/pasa-altos tiene UNA sola frecuencia de corte (no un
    // rango) — el "20Hz" que tenía la versión anterior como límite inferior
    // del lowpass no tiene sentido para ese tipo de filtro, ya que el
    // lowpass deja pasar DC y todo lo de abajo de fc por definición.
    const float fc_low_norm  = 300.0f  / (float)SAMPLE_RATE;   // frontera banda baja / media
    const float fc_high_norm = 3000.0f / (float)SAMPLE_RATE;   // frontera banda media / alta

    fir_design_lowpass (lp_coeffs, fc_low_norm);                   // LPF: pasa DC .. 300 Hz
    fir_design_bandpass(bp_coeffs, fc_low_norm, fc_high_norm);     // BPF: pasa 300 Hz .. 3 kHz
    fir_design_highpass(hp_coeffs, fc_high_norm);                  // HPF: pasa 3 kHz .. Nyquist

    for (int ch = 0; ch < CHANNELS; ch++) {
        memset(delay_lp[ch], 0, sizeof(delay_lp[ch]));
        memset(delay_bp[ch], 0, sizeof(delay_bp[ch]));
        memset(delay_hp[ch], 0, sizeof(delay_hp[ch]));

        // último parámetro decimation = 1: sin diezmar, una salida por muestra de entrada
        dsps_fird_init_f32(&fir_lp[ch], lp_coeffs, delay_lp[ch], FIR_TAPS, 1);
        dsps_fird_init_f32(&fir_bp[ch], bp_coeffs, delay_bp[ch], FIR_TAPS, 1);
        dsps_fird_init_f32(&fir_hp[ch], hp_coeffs, delay_hp[ch], FIR_TAPS, 1);
    }
}

// =============================================================================
// Tarea DSP (core 0, prioridad media)
// =============================================================================

static void dsp_task(void *pv)
{
    const float SCALE_IN  = 1.0f / (float)0x7FFFFF;
    const float SCALE_OUT = (float)0x7FFFFF;
    const float CLAMP_MAX =  (float)0x7FFFFF;
    const float CLAMP_MIN = -(float)0x800000;

    audio_buffer_t *buf;

    ESP_LOGI(TAG,"DSP task arrancó");
    while (1) {     
        if (xQueueReceive(dsp_queue, &buf, portMAX_DELAY) != pdPASS) continue; 
        // debería ser imposible que falle porque el guardián siempre  se activa primero y
        // envía un mensaje antes de esperar el siguiente, pero chequeamos por las dudas
        eq_params_t p;
        if (xQueuePeek(param_config_queue, &p, 0) != pdPASS) p = EQ_DEFAULTS; 
        // fallback a valores por defecto si no hay parámetros disponibles (no debería pasar)
        float gl = db_to_linear(p.gain_db_low);
        float gm = db_to_linear(p.gain_db_mid);
        float gh = db_to_linear(p.gain_db_high);
        float gg = db_to_linear(MASTER_GAIN);
        // ── 1. desintercalado int32_t estéreo → float mono por canal ──────────
        // El PCM1808 entrega 24 bits justificados a la izquierda en int32_t,
        // los 8 bits bajos son siempre 0 → shift derecho para normalizar.
        for (int i = 0; i < FRAMES_PER_BUF; i++) {
            ch_in[0][i] = (float)((*buf)[i * 2    ] >> 8) * SCALE_IN;  // L
            ch_in[1][i] = (float)((*buf)[i * 2 + 1] >> 8) * SCALE_IN;  // R
        }

        // ── 2. Aplicar los 3 filtros en paralelo sobre cada canal ─────────────
        // dsps_fird_f32 procesa FRAMES_PER_BUF muestras en una sola llamada,
        // usando el motor SIMD del Xtensa (instrucción ee.vmulas.s16.qacc).
        // Los 6 filtros son independientes y usan su propia línea de retardo.
        for (int ch = 0; ch < CHANNELS; ch++) {
            dsps_fird_f32(&fir_lp[ch], ch_in[ch], out_lp[ch], FRAMES_PER_BUF);
            dsps_fird_f32(&fir_bp[ch], ch_in[ch], out_bp[ch], FRAMES_PER_BUF);
            dsps_fird_f32(&fir_hp[ch], ch_in[ch], out_hp[ch], FRAMES_PER_BUF);
        }

        // ── 3. Suma con ganancia + intercalado + conversión a int32_t ─────────
        for (int i = 0; i < FRAMES_PER_BUF; i++) {
            float outL = (gl * out_lp[0][i] + gm * out_bp[0][i] + gh * out_hp[0][i])*gg;
            float outR = (gl * out_lp[1][i] + gm * out_bp[1][i] + gh * out_hp[1][i])*gg;

            outL *= SCALE_OUT;
            outR *= SCALE_OUT;
            outL = outL > CLAMP_MAX ? CLAMP_MAX : (outL < CLAMP_MIN ? CLAMP_MIN : outL); //recorto si saturo ADC
            outR = outR > CLAMP_MAX ? CLAMP_MAX : (outR < CLAMP_MIN ? CLAMP_MIN : outR); //recorto si saturo ADC

            (*buf)[i * 2    ] = (int32_t)outL << 8;  // L justificado izquierda
            (*buf)[i * 2 + 1] = (int32_t)outR << 8;  // R justificado izquierda
        }
        // Notificar al guardián que el buffer está listo
        xSemaphoreGive(dsp_done_sem);
    }
}

// =============================================================================
// Tarea Guardiana I2S (core 0, prioridad alta)
// =============================================================================
// Única responsable de i2s_channel_read/write y de la rotación del triple buffer.
// Se despierta desde la ISR

static void guardian_task(void *pv)
{
    // ── Arranque ─────────────────────────────────────────────────────────────
    // Escribo ceros para que el DAC no emita ruido mientras el DSP no ha
    // procesado nada todavía. Usa el slot tx actual (inicializado en cero).
    ESP_LOGI(TAG, "Antes de write inicial");   
    size_t written;
    i2s_channel_write(i2s_tx_handle,
                      *buf_tx,
                      sizeof(audio_buffers[0]),
                      &written,
                      portMAX_DELAY);
    ESP_LOGI(TAG,
         "TX written=%u first=%08lx second=%08lx",
         written,
         (uint32_t)(*buf_tx)[0],
         (uint32_t)(*buf_tx)[1]);

    //ESP_LOGI(TAG, "Despues de write, written=%d", written);

    // Primer read bloqueante — sincroniza con el ADC
    size_t read;
    i2s_channel_read(i2s_rx_handle,
                     *buf_rx,
                     sizeof(audio_buffers[0]),
                     &read,
                     portMAX_DELAY);
    ESP_LOGI(TAG,"RX bytes=%d", read);

    // Rotación inicial: el slot recién leído pasa al DSP.
    // Intercambio de 3 punteros — no hay índices que recalcular.
    {
        audio_buffer_t *old_rx = buf_rx;
        buf_rx  = buf_tx;    // TX ya fue enviado → libre para el ADC
        buf_tx  = buf_dsp;   // DSP tenía silencio → pasa al DAC (silencio al inicio)
        buf_dsp = old_rx;    // RX recién leído → va al DSP
    }

    ESP_LOGI(TAG,"Mandando buffer DSP");
    xQueueOverwrite(dsp_queue, &buf_dsp);

    // ── Bucle principal ───────────────────────────────────────────────────────
    while (1) {
        // Esperar notificación de la ISR (un buffer ADC nuevo está disponible en el DMA)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // Leer buffer del ADC → slot RX actual.
        // El dato ya está disponible porque la ISR nos despertó.
        i2s_channel_read(i2s_rx_handle,
                         *buf_rx,
                         sizeof(audio_buffers[0]),
                         &read,
                         0);

        // Escribir al DAC desde el slot TX (procesado por el DSP en el ciclo anterior)
        i2s_channel_write(i2s_tx_handle,
                          *buf_tx,
                          sizeof(audio_buffers[0]),
                          &written,
                          0);
        
        // Esperar que el DSP haya terminado con buf_dsp antes de rotarlo
        xSemaphoreTake(dsp_done_sem, portMAX_DELAY);
        // Rotar roles — intercambio de 3 punteros:
        //   viejo RX  (recién leído)      → DSP
        //   viejo DSP (ya procesado)      → TX
        //   viejo TX  (ya enviado al DAC) → RX (libre para el próximo ADC)
        audio_buffer_t *old_rx = buf_rx;
        buf_rx  = buf_tx;
        buf_tx  = buf_dsp;
        buf_dsp = old_rx;
        /*
         Al enviar el puntero del nuevo buffer al DSP, si el DSP aún no terminó de
         procesar el anterior, no queremos que se bloquee la tarea guardiana.
         Con xQueueOverwrite lo reemplaza en la cola, asegurando que si por
         alguna razón el DSP no responde a tiempo la guardiana nunca se bloquee.
        */
        taskYIELD();                            //a veces el programa se traba, yield por si acaso             
        xQueueOverwrite(dsp_queue, &buf_dsp);   // enviar el nuevo buffer al DSP
    }
}

// =============================================================================
// Tarea UART (core 1) — recibe comandos de ganancia
// =============================================================================
// Sigue el ejemplo uart_events de Espressif
// Protocolo simple por texto:
//   "L<valor>"  → ganancia banda baja  en dB  (ej: "L-3.5")
//   "M<valor>"  → ganancia banda media en dB
//   "H<valor>"  → ganancia banda alta  en dB
// Responde "OK\n" o "ERR: fuera de rango\n"

static void uart_task(void *pv)
{
    const uart_config_t uart_cfg = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_GPIO, UART_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // queue_size = UART_EVENT_QUEUE_LEN crea la cola nativa de eventos del
    // driver, entregada por puntero en uart_event_queue.
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0,
                                        UART_EVENT_QUEUE_LEN, &uart_event_queue, 0));

    uart_event_t event;
    uint8_t      data[UART_MSG_MAX_LEN];

    while (1) {
        // Bloquea hasta que el driver UART encole un evento (push hecho
        // desde su ISR interna ante cada paquete recibido por DMA/FIFO).
        if (xQueueReceive(uart_event_queue, &event, portMAX_DELAY) != pdPASS) 
        {
            continue;
        }

        if (event.type == UART_DATA) {
            // event.size: bytes disponibles en el buffer interno del driver
            size_t to_read = event.size;
            if (to_read > sizeof(data) - 1) to_read = sizeof(data) - 1;

            int len = uart_read_bytes(UART_PORT, data, to_read, 0);
            if (len <= 0) continue;
            data[len] = '\0';

            float val;
            char  cmd = (char)data[0];

            if ((cmd == 'L' || cmd == 'l' ||
                 cmd == 'M' || cmd == 'm' ||
                 cmd == 'H' || cmd == 'h') &&
                sscanf((char *)data + 1, "%f", &val) == 1)
            {
                if (val < GAIN_DB_MIN || val > GAIN_DB_MAX) {
                    uart_write_bytes(UART_PORT, "ERR: fuera de rango\n", 20);
                    continue;
                }

                // Leer estado actual, modificar el campo correspondiente, reescribir.
                // xQueuePeek + xQueueOverwrite: lectura no destructiva, escritura atómica.
                eq_params_t p;
                if (xQueuePeek(param_config_queue, &p, 0) != pdPASS) p = EQ_DEFAULTS;

                if      (cmd == 'L' || cmd == 'l') p.gain_db_low  = val;
                else if (cmd == 'M' || cmd == 'm') p.gain_db_mid  = val;
                else                               p.gain_db_high = val;

                xQueueOverwrite(param_config_queue, &p);

                // Avisar a flash_task que hubo un cambio de ganancia para que
                xSemaphoreGive(change_semaphore);

                uart_write_bytes(UART_PORT, "OK\n", 3);
                ESP_LOGI(TAG, "Ganancia %c = %.1f dB", cmd, val);
            }

        } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            // Recuperar el driver descartando lo acumulado
            uart_flush_input(UART_PORT);
            xQueueReset(uart_event_queue); // limpiar la cola de eventos para evitar procesar eventos obsoletos
            ESP_LOGW(TAG, "UART overflow — buffer reiniciado"); //aviso en logs sin bloquear sistema
        }
        // Otros tipos de evento (BREAK, PARITY_ERR, etc.) se ignoran aquí.
    }
}

// =============================================================================
// Encoder rotativo — ISRs + tarea (core 1)
// =============================================================================
<<<<<<< Updated upstream
//
// Pinout real del encoder
//   CLK = señal A    DT = señal B    SW = pulsador
//
//   La ISR solo se dispara en el FLANCO DESCENDENTE de CLK.
//   En ese instante se lee el nivel de DT:
//     DT == 1 (alto)  → giro CW  (DT va "adelantado" respecto a CLK)
//     DT == 0 (bajo)  → giro CCW (DT va "atrasado"   respecto a CLK)
//   Es el método solo tiene 1 problema, si el usuario hace solo un paso
//   en donde CLK no hace un flanco descendente no se detecta el giro
//   por ser gray, pero es simple de implementar y no requiere antirrebotes.
//   Por otra parte, el pulsador SW tambien se detecta con flanco descendente 
//   y si tiene debounce por software.

#define ENCODER_SW_DEBOUNCE_MS  50

// ISR de CLK — único flanco que dispara la lectura de dirección
static void IRAM_ATTR encoder_clk_isr(void *arg)
{
    gpio_intr_disable(ENCODER_CLK);
    int dt = gpio_get_level(ENCODER_DT);
    encoder_event_t evt = dt ? ENCODER_EVT_ROTATE_CW : ENCODER_EVT_ROTATE_CCW;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(encoder_queue, &evt, &woken);
    portYIELD_FROM_ISR(woken);
}

// ISR del pulsador SW — flanco descendente (activo en bajo)
static void IRAM_ATTR encoder_sw_isr(void *arg)
{
    // Deshabilitar la ISR del SW para debounce — la tarea la rehabilita
    gpio_intr_disable(ENCODER_SW);

    encoder_event_t evt = ENCODER_EVT_BUTTON;
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(encoder_queue, &evt, &woken);
    portYIELD_FROM_ISR(woken);
}

static void encoder_task(void *pv)
{
    // ── Configurar GPIOs ────────────────────────────────────────────────────
    // CLK: solo flanco descendente — es la única transición que dispara lectura
    gpio_config_t clk_cfg = {
        .pin_bit_mask = ((uint64_t)1 << ENCODER_CLK), // unit64_t porque tengo gpio mayor a 31
=======
//tomado de ejemplo de Espressif:

static void encoder_task(void *pv)
{
    ESP_LOGI(TAG, "encoder_task arrancó");
    
    // ── Configurar PCNT ─────────────────────────────────────────────────────
    pcnt_unit_config_t unit_cfg = {
        .high_limit = 100,
        .low_limit  = -100,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &encoder_pcnt_unit));
    ESP_LOGI(TAG, "pcnt_new_unit OK");

    // Filtro antirebote: ignora pulsos más cortos que 2000 ns
    //pcnt_glitch_filter_config_t filter_cfg = { .max_glitch_ns = 2000 };
    //ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(encoder_pcnt_unit, &filter_cfg));
    ESP_LOGI(TAG, "glitch_filter OK");

    // Canal A: cuenta en flanco de CLK, con dirección dada por DT
    pcnt_chan_config_t chan_a_cfg = {
        .edge_gpio_num  = ENCODER_CLK,
        .level_gpio_num = ENCODER_DT,
    };
    pcnt_channel_handle_t chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(encoder_pcnt_unit, &chan_a_cfg, &chan_a));

    // Canal B: cuenta en flanco de DT, con dirección dada por CLK
    pcnt_chan_config_t chan_b_cfg = {
        .edge_gpio_num  = ENCODER_DT,
        .level_gpio_num = ENCODER_CLK,
    };
    pcnt_channel_handle_t chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(encoder_pcnt_unit, &chan_b_cfg, &chan_b));

    // Acciones: decrementa/incrementa según flanco y nivel del otro canal
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // Configurar SW como entrada con pull-up (sin PCNT, es un botón simple)
    gpio_config_t sw_cfg = {
        .pin_bit_mask = (int64_t 1 << ENCODER_SW),
>>>>>>> Stashed changes
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&clk_cfg);

    // DT: entrada simple, sin interrupción — solo se lee su nivel desde la ISR de CLK
    gpio_config_t dt_cfg = {
        .pin_bit_mask = ((uint64_t)1 << ENCODER_DT), // unit64_t porque tengo gpio mayor a 31
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&dt_cfg);

    // SW: flanco descendente (pulsación)
    gpio_config_t sw_cfg = {
        .pin_bit_mask = ((uint64_t)1 << ENCODER_SW), // unit64_t porque tengo gpio mayor a 31
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&sw_cfg);

    // Registrar ISRs (servicio de ISR de GPIO debe estar instalado — ver app_main)
    gpio_isr_handler_add(ENCODER_CLK, encoder_clk_isr, NULL);
    gpio_isr_handler_add(ENCODER_SW,  encoder_sw_isr,  NULL);

    encoder_event_t evt;
    while (1) {
        // Bloquear hasta que alguna ISR encole un evento
        if (xQueueReceive(encoder_queue, &evt, portMAX_DELAY) != pdPASS) continue;

        // Leer parámetros actuales — read-modify-write sobre la cola
        eq_params_t p;
        if (xQueuePeek(param_config_queue, &p, 0) != pdPASS) p = EQ_DEFAULTS;

        if (evt == ENCODER_EVT_ROTATE_CW || evt == ENCODER_EVT_ROTATE_CCW) {
            float delta = (evt == ENCODER_EVT_ROTATE_CW) ? +1.0f : -1.0f;

            float *g = (p.selected_band == 0) ? &p.gain_db_low
                     : (p.selected_band == 1) ? &p.gain_db_mid
                                              : &p.gain_db_high;
            *g += delta;
            if (*g > GAIN_DB_MAX) *g = GAIN_DB_MAX;
            if (*g < GAIN_DB_MIN) *g = GAIN_DB_MIN;

            xQueueOverwrite(param_config_queue, &p);

            // Rehabilitar ISR del SW luego del período de debounce
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_intr_enable(ENCODER_CLK);

            // Hubo cambio de ganancia → avisar a flash_task para que persista.
            xSemaphoreGive(change_semaphore);

        } else if (evt == ENCODER_EVT_BUTTON) {
            p.selected_band = (p.selected_band + 1) % 3;
            xQueueOverwrite(param_config_queue, &p);

            ESP_LOGI(TAG, "Banda seleccionada: %s",
                     p.selected_band == 0 ? "LOW" :
                     p.selected_band == 1 ? "MID" : "HIGH");

            // Cambiar de banda no modifica ganancia → no se notifica a flash_task

            // Rehabilitar ISR del SW luego del período de debounce
            vTaskDelay(pdMS_TO_TICKS(ENCODER_SW_DEBOUNCE_MS));
            gpio_intr_enable(ENCODER_SW);
        }
    }
<<<<<<< Updated upstream
=======

    // Resetear el contador cuando se aleja del centro para evitar overflow en los límites
    if (ultimo_conteo > 50 || ultimo_conteo < -50) {
        pcnt_unit_clear_count(encoder_pcnt_unit);
        ultimo_conteo = 0;
    }

    // ── Pulsador SW ─────────────────────────────────────────────────────────
    bool sw_actual = gpio_get_level(ENCODER_SW); //por polling, no ISR
    if (!sw_actual && sw_ultimo) {
        eq_params_t p;
        if (xQueuePeek(param_config_queue, &p, 0) != pdPASS) p = EQ_DEFAULTS;

        p.selected_band = (p.selected_band + 1) % 3;
        xQueueOverwrite(param_config_queue, &p);

        ESP_LOGI(TAG, "Banda seleccionada: %s",
                 p.selected_band == 0 ? "LOW" :
                 p.selected_band == 1 ? "MID" : "HIGH");

        vTaskDelay(pdMS_TO_TICKS(200));  // debounce del botón
    }
    sw_ultimo = sw_actual;

    vTaskDelay(pdMS_TO_TICKS(10));
}
>>>>>>> Stashed changes
}

// =============================================================================
// Tarea Flash / NVS (core 1) — persiste ganancias solo cuando cambian
// =============================================================================


static void flash_task(void *pv)
{
    nvs_handle_t nvs;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs));

    while (1) {
        // Bloquea hasta que uart_task o encoder_task avisen un cambio de ganancia
        if (xSemaphoreTake(change_semaphore, portMAX_DELAY) != pdTRUE) continue;

        // Leer parámetros sin consumirlos
        eq_params_t p;
        if (xQueuePeek(param_config_queue, &p, 0) != pdPASS) continue;

        // Union para type-punning seguro (vs. cast de puntero que es UB en C estricto)
        union { float f; int32_t i; } u;

        u.f = p.gain_db_low;  nvs_set_i32(nvs, NVS_KEY_LOW,  u.i);
        u.f = p.gain_db_mid;  nvs_set_i32(nvs, NVS_KEY_MID,  u.i);
        u.f = p.gain_db_high; nvs_set_i32(nvs, NVS_KEY_HIGH, u.i);
        esp_err_t err = nvs_commit(nvs);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Ganancias guardadas: L=%.1f M=%.1f H=%.1f dB",
                     p.gain_db_low, p.gain_db_mid, p.gain_db_high);
        } else {
            ESP_LOGW(TAG, "Error guardando NVS: %s", esp_err_to_name(err));
        }
    }
}

// =============================================================================
// Tarea LCD I2C (core 1) — muestra ganancias actuales
// =============================================================================
// Hardware: LCD2004 (20 columnas x 4 filas, HD44780) detrás de un backpack
// PCF8574A, dirección 0x3F. Actualiza el display cada 1 segundo.

// ── Mapa de bits del backpack PCF8574A ───────────────────────────────────────
#define LCD_BIT_RS   (1 << 0)
#define LCD_BIT_RW   (1 << 1)
#define LCD_BIT_EN   (1 << 2)
#define LCD_BIT_BL   (1 << 3)   
#define LCD_BIT_D4   (1 << 4)
#define LCD_BIT_D5   (1 << 5)
#define LCD_BIT_D6   (1 << 6)
#define LCD_BIT_D7   (1 << 7)

// Backlight se mantiene encendido por defecto en todas las escrituras.
static uint8_t lcd_backlight = LCD_BIT_BL;

// ── Primitiva de bajo nivel: escribe un byte crudo al PCF8574A ─────────────
static esp_err_t lcd_i2c_write(uint8_t data)
{
    return i2c_master_write_to_device(I2C_NUM_0, LCD_I2C_ADDR,
                                      &data, 1, pdMS_TO_TICKS(50));
}

// Pulsa EN: el HD44780 lee los 4 bits de datos en el flanco descendente de EN.
// Los anchos de pulso (>450ns alto, >37us de ciclo) se cumplen sobrados por
// la latencia natural de una transacción I2C completa, no se necesita delay.
static void lcd_pulse_enable(uint8_t data)
{
    lcd_i2c_write(data | LCD_BIT_EN);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_i2c_write(data & ~LCD_BIT_EN);
    vTaskDelay(pdMS_TO_TICKS(1));
}

// Envía un nibble (4 bits altos de 'nibble') manteniendo RS y backlight
static void lcd_write_nibble(uint8_t nibble, bool is_data)
{
    uint8_t data = lcd_backlight | (is_data ? LCD_BIT_RS : 0);
    if (nibble & 0x01) data |= LCD_BIT_D4;
    if (nibble & 0x02) data |= LCD_BIT_D5;
    if (nibble & 0x04) data |= LCD_BIT_D6;
    if (nibble & 0x08) data |= LCD_BIT_D7;

    lcd_pulse_enable(data);
}

// Envía un byte completo: nibble alto primero, luego nibble bajo.
// is_data: true → byte de datos (RS=1, va a DDRAM/CGRAM)
//          false → byte de comando (RS=0, instrucción del HD44780)
static void lcd_write_byte(uint8_t byte, bool is_data)
{
    lcd_write_nibble((byte >> 4) & 0x0F, is_data);
    lcd_write_nibble(byte & 0x0F, is_data);
}

static inline void lcd_command(uint8_t cmd)  { lcd_write_byte(cmd, false); }
static inline void lcd_data(uint8_t data)    { lcd_write_byte(data, true); }

// ── Secuencia de inicialización estándar del HD44780 en modo 4 bits ─────────
// Sigue la secuencia del datasheet: 3 pulsos en 8-bit seguidos del cambio a
// 4-bit, necesarios porque el controlador puede arrancar en un estado
// desconocido y estos pulsos lo sincronizan sin depender de leer su estado.
static void lcd_init(void)
{
    vTaskDelay(pdMS_TO_TICKS(50));   // esperar Vcc estable (>40ms tras power-on)

    // Forzar modo 8-bit tres veces — secuencia de "despertar" del HD44780
    lcd_write_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(1));
    lcd_write_nibble(0x03, false);
    vTaskDelay(pdMS_TO_TICKS(1));

    // Cambiar a modo 4-bit — a partir de aquí todo se envía en dos nibbles
    lcd_write_nibble(0x02, false);
    vTaskDelay(pdMS_TO_TICKS(1));

    // Function set: 4-bit, 2 líneas, fuente de caracteres 5x8
    lcd_command(0x28);
    // Display off, cursor off, blink off (apagar antes de configurar)
    lcd_command(0x08);
    // Clear display
    lcd_command(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));   // clear necesita >1.52ms
    // Entry mode set: incrementar cursor, sin shift de display
    lcd_command(0x06);
    // Display on, cursor off, blink off
    lcd_command(0x0C);
}


// Direcciones DDRAM de inicio de fila para un LCD2004 (controlador HD44780
// con 4 líneas reales, no las 2 internas — el mapeo de filas 2 y 3 salta
// 20 posiciones respecto al layout de 2 líneas).
static const uint8_t LCD_ROW_OFFSETS[4] = {0x00, 0x40, 0x14, 0x54};

static void lcd_set_cursor(int row, int col)
{
    if (row < 0 || row >= LCD_ROWS) row = 0;
    if (col < 0 || col >= LCD_COLS) col = 0;
    lcd_command(0x80 | (LCD_ROW_OFFSETS[row] + col));
}

static void lcd_print(const char *s)
{
    while (*s) {
        lcd_data((uint8_t)*s);
        s++;
    }
}

static void lcd_task(void *pv)
{
    lcd_set_cursor(0, 0);
    lcd_print("Ganancia actual:");

    char line_high[48];
    char line_mid [48];
    char line_low [48];
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        // Bloqueo periódico preciso a 1 segundo: no se acumula deriva
        // aunque el cuerpo del bucle (transacciones I2C) tarde variablemente.
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(250));

        // Leer parámetros sin consumirlos — otras tareas también los leen
        eq_params_t p;
        if (xQueuePeek(param_config_queue, &p, 0) != pdPASS) continue;

        snprintf(line_high, sizeof(line_high), "High: %+3.0fdB%*s",
                 p.gain_db_high, 8, p.selected_band == 2 ? "<" : "");
        snprintf(line_mid,  sizeof(line_mid),  "Mid:  %+3.0fdB%*s",
                 p.gain_db_mid,  8, p.selected_band == 1 ? "<" : "");
        snprintf(line_low,  sizeof(line_low),  "Low:  %+3.0fdB%*s",
                 p.gain_db_low,  8, p.selected_band == 0 ? "<" : "");

        lcd_set_cursor(1, 0);
        lcd_print(line_high);
        lcd_set_cursor(2, 0);
        lcd_print(line_mid);
        lcd_set_cursor(3, 0);
        lcd_print(line_low);
    }
}


// =============================================================================
// app_main
// =============================================================================

void app_main(void)
{
    // ── NVS ──────────────────────────────────────────────────────────────────
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // ── Cola de parámetros: profundidad 1, tipo eq_params_t ──────────────────
    param_config_queue = xQueueCreate(1, sizeof(eq_params_t));
    configASSERT(param_config_queue);

    // ── Cargar ganancias desde NVS (si existen de una sesión anterior) ────────
    {
        eq_params_t p = EQ_DEFAULTS;
        nvs_handle_t nvs;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
            union { float f; int32_t i; } u;
            if (nvs_get_i32(nvs, NVS_KEY_LOW,  &u.i) == ESP_OK) p.gain_db_low  = u.f;
            if (nvs_get_i32(nvs, NVS_KEY_MID,  &u.i) == ESP_OK) p.gain_db_mid  = u.f;
            if (nvs_get_i32(nvs, NVS_KEY_HIGH, &u.i) == ESP_OK) p.gain_db_high = u.f;
            nvs_close(nvs);
            ESP_LOGI(TAG, "Ganancias cargadas: L=%.1f M=%.1f H=%.1f dB",
                     p.gain_db_low, p.gain_db_mid, p.gain_db_high);
        }
        // Publicar estado inicial — la cola queda con un mensaje válido desde el arranque
        xQueueOverwrite(param_config_queue, &p);
    }

    // ── Triple buffer ─────────────────────────────────────────────────────────
    memset(audio_buffers, 0, sizeof(audio_buffers));

    
    // Inicializar I2C
    const i2c_config_t i2c_cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA_GPIO,
        .scl_io_num       = I2C_SCL_GPIO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    lcd_init();
    ESP_LOGI("LCD","Init terminado");


    // ── I2S ──────────────────────────────────────────────────────────────────
    //ESP_ERROR_CHECK(i2s_init_full_duplex());

    // ── Inicialización de DSP ───────────────────────────────────────────────

    ESP_ERROR_CHECK(dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE));

    // ── Cola DSP ──────────────────────────────────────────────────────────────
    dsp_queue = xQueueCreate(1, sizeof(audio_buffer_t *));
    configASSERT(dsp_queue);

    // ── Semáforo binario de cambio de ganancia ────────────────────────────────
    // Lo entregan uart_task y encoder_task; lo toma flash_task para persistir.
    change_semaphore = xSemaphoreCreateBinary();
    configASSERT(change_semaphore);

    // ── Semáforo binario de finalizacion DSP ────────────────────────────────
    // Lo entrega DSP
    dsp_done_sem = xSemaphoreCreateBinary();
    configASSERT(dsp_done_sem);

    // ── Coeficientes FIR ─────────────────────────────────────────────────────
    init_fir_bands();

<<<<<<< Updated upstream
    // ── Servicio de ISR de GPIO (necesario para gpio_isr_handler_add) ────────
    // ESP_INTR_FLAG_IRAM: la ISR puede ejecutarse aunque la caché esté ocupada
    gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

=======
>>>>>>> Stashed changes
    // ── Tareas ────────────────────────────────────────────────────────────────
    // Core 0 — audio en tiempo real
    xTaskCreatePinnedToCore(guardian_task, "guardian",
                            STACK_GUARDIAN, NULL,
                            configMAX_PRIORITIES - 1,
                            &guardian_handle, 0);
    configASSERT(guardian_handle);

    ESP_ERROR_CHECK(i2s_init_full_duplex());

    xTaskCreatePinnedToCore(dsp_task, "dsp",
                            STACK_DSP, NULL,
                            configMAX_PRIORITIES - 2,
                            NULL, 0);

    // Core 1 — control y periféricos
    xTaskCreatePinnedToCore(encoder_task, "encoder", STACK_AUX, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(uart_task,    "uart",    STACK_AUX, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(flash_task,   "flash",   STACK_AUX, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(lcd_task,     "lcd",     STACK_AUX, NULL, 2, NULL, 1);

    ESP_LOGI(TAG, "Sistema iniciado. fs=%d Hz, buf=%d frames, FIR=%d taps",
             SAMPLE_RATE, FRAMES_PER_BUF, FIR_TAPS);
}