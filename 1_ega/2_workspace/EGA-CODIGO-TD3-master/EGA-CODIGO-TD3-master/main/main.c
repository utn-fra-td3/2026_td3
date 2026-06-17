#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/uart.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "hd44780.h"
#include "nvs_flash.h"
#include "nvs.h"

// ============================================================
// TAG
// ============================================================

static const char *TAG = "TD3_PID";

// ============================================================
// PINES ESP32-S3
// ============================================================

// L298N
#define PIN_MOTOR_PWM       GPIO_NUM_4
#define PIN_MOTOR_IN1       GPIO_NUM_5
#define PIN_MOTOR_IN2       GPIO_NUM_6

// AS5600
#define PIN_AS5600_DIR      GPIO_NUM_7

// I2C compartido: AS5600 + LCD
#define PIN_I2C_SDA         GPIO_NUM_8
#define PIN_I2C_SCL         GPIO_NUM_9

// Botones
#define PIN_BTN_PLUS        GPIO_NUM_10
#define PIN_BTN_MINUS       GPIO_NUM_11
#define PIN_BTN_MENU        GPIO_NUM_12
#define PIN_BTN_OK          GPIO_NUM_13

// UART comandos PC
#define PIN_UART_TX         GPIO_NUM_17
#define PIN_UART_RX         GPIO_NUM_18

// ============================================================
// I2C
// ============================================================

#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         100000

#define AS5600_ADDR         0x36
#define AS5600_REG_ANGLE_H  0x0E

#define LCD_I2C_ADDR        0x27
#define LCD_I2C_TIMEOUT_MS  50
#define LCD_COLS            16
#define LCD_ROWS            2

// ============================================================
// PWM LEDC
// ============================================================

#define LEDC_MODE_USED      LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_USED     LEDC_TIMER_0
#define LEDC_CHANNEL_USED   LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT
#define LEDC_MAX_DUTY       1023
#define LEDC_FREQ_HZ        20000

// ============================================================
// UART
// ============================================================

#define UART_PORT           UART_NUM_1
#define UART_BUF_SIZE       256

// ============================================================
// CONTROL
// ============================================================

#define PID_PERIOD_MS       10
#define AS5600_PERIOD_MS    10

#define ANGLE_TOLERANCE_DEG 2.0f

#define PWM_MAX             1023.0f
#define PWM_MIN             150.0f

#define BLOCK_PWM_MIN       250.0f
#define BLOCK_ERROR_MIN     5.0f
#define BLOCK_DELTA_MIN     1.0f
#define BLOCK_COUNTER_LIMIT 50

// ============================================================
// TIPOS
// ============================================================

typedef enum {
    PROFILE_ESCALON = 0,
    PROFILE_RAMPA,

    PROFILE_FAST = PROFILE_ESCALON,
    PROFILE_SMOOTH = PROFILE_RAMPA
} movement_profile_t;

typedef enum {
    CMD_SET_ANGLE = 0,
    CMD_SET_KP,
    CMD_SET_KI,
    CMD_SET_KD,
    CMD_SET_PROFILE,
    CMD_STOP,
    CMD_BLOCK_DETECTED,
    CMD_REVERSE,
    CMD_LOAD_CONFIG
} control_cmd_type_t;

typedef struct {
    control_cmd_type_t type;
    float value;
} control_cmd_t;

typedef enum {
    I2C_REQ_READ_AS5600 = 0,
    I2C_REQ_LCD_INIT,
    I2C_REQ_LCD_PRINT
} i2c_req_type_t;

typedef struct {
    i2c_req_type_t type;
    char lcd_line1[LCD_COLS + 1];
    char lcd_line2[LCD_COLS + 1];
} i2c_request_t;

typedef struct {
    bool ok;
    uint16_t as5600_raw;
} i2c_response_t;

typedef struct {
    float setpoint;
    float position;
    float error;
    float pwm;
    int direction;
    bool moving;
} motor_state_t;

typedef enum {
    DISPLAY_SHOW_MENU = 0,
    DISPLAY_SHOW_SETPOINT,
    DISPLAY_SHOW_KP,
    DISPLAY_SHOW_KI,
    DISPLAY_SHOW_KD,
    DISPLAY_SHOW_PROFILE,
    DISPLAY_SHOW_MESSAGE
} display_msg_type_t;

typedef struct {
    display_msg_type_t type;
    float value;
    char text[32];
} display_msg_t;

typedef enum {
    MENU_ANGLE = 0,
    MENU_KP,
    MENU_KI,
    MENU_KD,
    MENU_PROFILE,
    MENU_COUNT
} ui_menu_t;

typedef struct {
    float setpoint;
    float kp;
    float ki;
    float kd;
    movement_profile_t profile;
} system_config_t;

typedef enum {
    CONFIG_SAVE_ALL = 0,
    CONFIG_LOAD_ALL
} config_cmd_type_t;

typedef struct {
    config_cmd_type_t type;
    system_config_t config;
} config_msg_t;

// ============================================================
// COLAS Y SEMÁFOROS
// ============================================================

static QueueHandle_t ControlQueue;
static QueueHandle_t I2C_TXQueue;
static QueueHandle_t I2C_RXQueue;
static QueueHandle_t angleQueue;
static QueueHandle_t MotorStateQueue;
static QueueHandle_t DisplayQueue;
static QueueHandle_t ConfigQueue;

static SemaphoreHandle_t sem_btn_plus;
static SemaphoreHandle_t sem_btn_minus;
static SemaphoreHandle_t sem_btn_menu;
static SemaphoreHandle_t sem_btn_ok;

// ============================================================
// CONFIGURACIÓN GLOBAL
// ============================================================

static system_config_t g_config = {
    .setpoint = 0.0f,
    .kp = 6.0f,
    .ki = 0.0f,
    .kd = 0.25f,
    .profile = PROFILE_ESCALON
};

// ============================================================
// FUNCIONES AUXILIARES
// ============================================================

static float normalize_angle(float angle)
{
    while (angle >= 360.0f) {
        angle -= 360.0f;
    }

    while (angle < 0.0f) {
        angle += 360.0f;
    }

    return angle;
}

static float calculate_angular_error(float setpoint, float position)
{
    float error = setpoint - position;

    while (error > 180.0f) {
        error -= 360.0f;
    }

    while (error < -180.0f) {
        error += 360.0f;
    }

    return error;
}

static float raw_to_degrees(uint16_t raw)
{
    return ((float)raw * 360.0f) / 4096.0f;
}

// ============================================================
// MOTOR L298N
// ============================================================

static void motor_stop(void)
{
    ledc_set_duty(LEDC_MODE_USED, LEDC_CHANNEL_USED, 0);
    ledc_update_duty(LEDC_MODE_USED, LEDC_CHANNEL_USED);

    gpio_set_level(PIN_MOTOR_IN1, 0);
    gpio_set_level(PIN_MOTOR_IN2, 0);
}

static void motor_apply(float control_signal)
{
    float pwm = fabsf(control_signal);

    if (pwm > PWM_MAX) {
        pwm = PWM_MAX;
    }

    if (pwm > 0.0f && pwm < PWM_MIN) {
        pwm = PWM_MIN;
    }

    if (control_signal > 0.0f) {
        gpio_set_level(PIN_MOTOR_IN1, 1);
        gpio_set_level(PIN_MOTOR_IN2, 0);
    } else if (control_signal < 0.0f) {
        gpio_set_level(PIN_MOTOR_IN1, 0);
        gpio_set_level(PIN_MOTOR_IN2, 1);
    } else {
        motor_stop();
        return;
    }

    ledc_set_duty(LEDC_MODE_USED, LEDC_CHANNEL_USED, (uint32_t)pwm);
    ledc_update_duty(LEDC_MODE_USED, LEDC_CHANNEL_USED);
}

// ============================================================
// I2C BAJO NIVEL
// ============================================================

static esp_err_t as5600_read_raw(uint16_t *raw)
{
    uint8_t reg = AS5600_REG_ANGLE_H;
    uint8_t data[2] = {0};

    esp_err_t err = i2c_master_write_read_device(
        I2C_PORT,
        AS5600_ADDR,
        &reg,
        1,
        data,
        2,
        pdMS_TO_TICKS(50)
    );

    if (err != ESP_OK) {
        return err;
    }

    *raw = ((uint16_t)data[0] << 8) | data[1];
    *raw &= 0x0FFF;

    return ESP_OK;
}

// LCD 16x2 sobre backpack PCF8574.
// Solo I2C_Manager_Task llama a estas funciones.
static hd44780_t lcd = {
    .write_cb = NULL,
    .font = HD44780_FONT_5X8,
    .lines = LCD_ROWS,
    .pins = {
        .rs = 0,
        .e = 2,
        .d4 = 4,
        .d5 = 5,
        .d6 = 6,
        .d7 = 7,
        .bl = 3,
    },
};

static esp_err_t lcd_write_cb(const hd44780_t *lcd_desc, uint8_t data)
{
    (void)lcd_desc;

    return i2c_master_write_to_device(
        I2C_PORT,
        LCD_I2C_ADDR,
        &data,
        1,
        pdMS_TO_TICKS(LCD_I2C_TIMEOUT_MS)
    );
}

static void lcd_format_line(char *dst, const char *src)
{
    size_t len = 0;

    memset(dst, ' ', LCD_COLS);
    dst[LCD_COLS] = '\0';

    while (src[len] != '\0' && len < LCD_COLS) {
        dst[len] = src[len];
        len++;
    }
}

static esp_err_t lcd_write_line(uint8_t row, const char *text)
{
    char line[LCD_COLS + 1];

    lcd_format_line(line, text);

    ESP_RETURN_ON_ERROR(hd44780_gotoxy(&lcd, 0, row), TAG, "LCD gotoxy failed");
    ESP_RETURN_ON_ERROR(hd44780_puts(&lcd, line), TAG, "LCD puts failed");

    return ESP_OK;
}

static esp_err_t lcd_write_screen(const char *line1, const char *line2)
{
    ESP_RETURN_ON_ERROR(lcd_write_line(0, line1), TAG, "LCD line 1 failed");
    ESP_RETURN_ON_ERROR(lcd_write_line(1, line2), TAG, "LCD line 2 failed");

    return ESP_OK;
}

static esp_err_t lcd_init_driver(void)
{
    lcd.write_cb = lcd_write_cb;

    ESP_RETURN_ON_ERROR(hd44780_init(&lcd), TAG, "LCD init failed");
    ESP_RETURN_ON_ERROR(hd44780_switch_backlight(&lcd, true), TAG, "LCD backlight failed");
    ESP_RETURN_ON_ERROR(hd44780_clear(&lcd), TAG, "LCD clear failed");
    ESP_RETURN_ON_ERROR(lcd_write_screen("TD3 PID", "Sistema iniciado"), TAG, "LCD splash failed");

    return ESP_OK;
}

// ============================================================
// NVS
// ============================================================

static void save_config_to_nvs(const system_config_t *cfg)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open("storage", NVS_READWRITE, &handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error abriendo NVS para escritura: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_blob(handle, "config", cfg, sizeof(system_config_t));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error guardando configuración: %s", esp_err_to_name(err));
        nvs_close(handle);
        return;
    }

    err = nvs_commit(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error haciendo commit en NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Configuración guardada en NVS");
    }

    nvs_close(handle);
}

static bool load_config_from_nvs(system_config_t *cfg)
{
    nvs_handle_t handle;
    size_t required_size = sizeof(system_config_t);

    esp_err_t err = nvs_open("storage", NVS_READONLY, &handle);

    if (err != ESP_OK) {
        return false;
    }

    err = nvs_get_blob(handle, "config", cfg, &required_size);
    nvs_close(handle);

    if (err == ESP_OK && required_size == sizeof(system_config_t)) {
        return true;
    }

    return false;
}

// ============================================================
// ISR BOTONES
// ============================================================

static void IRAM_ATTR gpio_button_isr_handler(void *arg)
{
    gpio_num_t pin = (gpio_num_t)(uint32_t)arg;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (pin == PIN_BTN_PLUS) {
        xSemaphoreGiveFromISR(sem_btn_plus, &higher_priority_task_woken);
    } else if (pin == PIN_BTN_MINUS) {
        xSemaphoreGiveFromISR(sem_btn_minus, &higher_priority_task_woken);
    } else if (pin == PIN_BTN_MENU) {
        xSemaphoreGiveFromISR(sem_btn_menu, &higher_priority_task_woken);
    } else if (pin == PIN_BTN_OK) {
        xSemaphoreGiveFromISR(sem_btn_ok, &higher_priority_task_woken);
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

// ============================================================
// TASK: I2C_MANAGER_TASK - PRIORIDAD 3
// ============================================================

static void I2C_Manager_Task(void *pvParameters)
{
    i2c_request_t req;
    i2c_response_t resp;
    bool lcd_ready = false;

    (void)pvParameters;

    while (1) {
        if (xQueueReceive(I2C_TXQueue, &req, portMAX_DELAY) == pdTRUE) {
            memset(&resp, 0, sizeof(resp));

            switch (req.type) {
                case I2C_REQ_READ_AS5600:
                    if (as5600_read_raw(&resp.as5600_raw) == ESP_OK) {
                        resp.ok = true;
                    } else {
                        resp.ok = false;
                    }

                    // I2C_RXQueue solo se usa para responder al AS5600_Reader_Task.
                    xQueueSend(I2C_RXQueue, &resp, portMAX_DELAY);
                    break;

                case I2C_REQ_LCD_INIT: {
                    esp_err_t ret = lcd_init_driver();

                    if (ret == ESP_OK) {
                        lcd_ready = true;
                        ESP_LOGI(TAG, "LCD inicializado");
                    } else {
                        ESP_LOGE(TAG, "No se pudo inicializar LCD: %s", esp_err_to_name(ret));
                    }
                    break;
                }

                case I2C_REQ_LCD_PRINT:
                    if (lcd_ready) {
                        esp_err_t ret = lcd_write_screen(req.lcd_line1, req.lcd_line2);

                        if (ret != ESP_OK) {
                            ESP_LOGE(TAG, "Error escribiendo LCD: %s", esp_err_to_name(ret));
                        }
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

// ============================================================
// TASK: AS5600_READER_TASK - PRIORIDAD 3
// ============================================================

static void AS5600_Reader_Task(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(AS5600_PERIOD_MS);

    i2c_request_t req = {
        .type = I2C_REQ_READ_AS5600
    };

    i2c_response_t resp;
    float angle = 0.0f;
    TickType_t last_log = 0;

    while (1) {
        xQueueSend(I2C_TXQueue, &req, portMAX_DELAY);

        if (xQueueReceive(I2C_RXQueue, &resp, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (resp.ok) {
                angle = raw_to_degrees(resp.as5600_raw);
                angle = normalize_angle(angle);

                // Cola de longitud 1: siempre queda la última posición.
                xQueueOverwrite(angleQueue, &angle);

                TickType_t now = xTaskGetTickCount();

                if ((now - last_log) >= pdMS_TO_TICKS(500)) {
                    ESP_LOGI(TAG, "AS5600 raw=%u angle=%.2f deg", (unsigned)resp.as5600_raw, angle);
                    last_log = now;
                }
            } else {
                ESP_LOGW(TAG, "Fallo lectura AS5600");
            }
        } else {
            ESP_LOGW(TAG, "Timeout esperando respuesta I2C AS5600");
        }

        vTaskDelayUntil(&last_wake, period);
    }
}

// ============================================================
// TASK: PID_TASK - PRIORIDAD 3
// ============================================================

static void PID_Task(void *pvParameters)
{
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(PID_PERIOD_MS);

    control_cmd_t cmd;
    motor_state_t motor_state;

    float setpoint = g_config.setpoint;
    float kp = g_config.kp;
    float ki = g_config.ki;
    float kd = g_config.kd;
    movement_profile_t profile = g_config.profile;

    float position = 0.0f;
    float error = 0.0f;
    float previous_error = 0.0f;
    float integral = 0.0f;
    float derivative = 0.0f;
    float output = 0.0f;

    bool moving = false;
    bool force_reverse = false;

    while (1) {
        if (!moving) {
            motor_stop();

            if (xQueueReceive(ControlQueue, &cmd, portMAX_DELAY) == pdTRUE) {
                switch (cmd.type) {
                    case CMD_SET_ANGLE:
                        setpoint = normalize_angle(cmd.value);
                        integral = 0.0f;
                        previous_error = 0.0f;
                        moving = true;
                        force_reverse = false;
                        ESP_LOGI(TAG, "Nuevo setpoint y movimiento: %.2f", setpoint);
                        break;

                    case CMD_SET_KP:
                        kp = cmd.value;
                        ESP_LOGI(TAG, "Kp = %.3f", kp);
                        break;

                    case CMD_SET_KI:
                        ki = cmd.value;
                        ESP_LOGI(TAG, "Ki = %.3f", ki);
                        break;

                    case CMD_SET_KD:
                        kd = cmd.value;
                        ESP_LOGI(TAG, "Kd = %.3f", kd);
                        break;

                    case CMD_SET_PROFILE:
                        profile = (movement_profile_t)((int)cmd.value);
                        ESP_LOGI(TAG, "Profile = %d", profile);
                        break;

                    case CMD_LOAD_CONFIG:
                        setpoint = g_config.setpoint;
                        kp = g_config.kp;
                        ki = g_config.ki;
                        kd = g_config.kd;
                        profile = g_config.profile;
                        ESP_LOGI(TAG, "Config cargada en PID");
                        break;

                    case CMD_STOP:
                        motor_stop();
                        moving = false;
                        break;

                    default:
                        break;
                }
            }
        } else {
            // Durante movimiento no se puede bloquear esperando ControlQueue.
            while (xQueueReceive(ControlQueue, &cmd, 0) == pdTRUE) {
                switch (cmd.type) {
                    case CMD_STOP:
                        moving = false;
                        force_reverse = false;
                        motor_stop();
                        ESP_LOGI(TAG, "STOP recibido");
                        break;

                    case CMD_SET_ANGLE:
                        setpoint = normalize_angle(cmd.value);
                        integral = 0.0f;
                        previous_error = 0.0f;
                        force_reverse = false;
                        ESP_LOGI(TAG, "Nuevo setpoint durante movimiento: %.2f", setpoint);
                        break;

                    case CMD_SET_KP:
                        kp = cmd.value;
                        break;

                    case CMD_SET_KI:
                        ki = cmd.value;
                        break;

                    case CMD_SET_KD:
                        kd = cmd.value;
                        break;

                    case CMD_SET_PROFILE:
                        profile = (movement_profile_t)((int)cmd.value);
                        break;

                    case CMD_BLOCK_DETECTED:
                    case CMD_REVERSE:
                        force_reverse = true;
                        integral = 0.0f;
                        ESP_LOGW(TAG, "Bloqueo detectado. Forzando sentido contrario.");
                        break;

                    case CMD_LOAD_CONFIG:
                        setpoint = g_config.setpoint;
                        kp = g_config.kp;
                        ki = g_config.ki;
                        kd = g_config.kd;
                        profile = g_config.profile;
                        break;

                    default:
                        break;
                }
            }

            if (!moving) {
                vTaskDelayUntil(&last_wake, period);
                continue;
            }

            if (xQueuePeek(angleQueue, &position, 0) != pdTRUE) {
                vTaskDelayUntil(&last_wake, period);
                continue;
            }

            error = calculate_angular_error(setpoint, position);

            // Ante bloqueo se intenta el camino contrario, aunque no sea el menor.
            if (force_reverse) {
                if (error > 0.0f) {
                    error = error - 360.0f;
                } else {
                    error = error + 360.0f;
                }
            }

            float dt = PID_PERIOD_MS / 1000.0f;

            integral += error * dt;

            // Antiwindup simple
            if (integral > 100.0f) {
                integral = 100.0f;
            }

            if (integral < -100.0f) {
                integral = -100.0f;
            }

            derivative = (error - previous_error) / dt;

            output = kp * error + ki * integral + kd * derivative;

            // Perfil RAMPA: reduce la salida del controlador.
            if (profile == PROFILE_RAMPA) {
                output *= 0.6f;
            }

            if (fabsf(error) <= ANGLE_TOLERANCE_DEG) {
                motor_stop();
                moving = false;
                force_reverse = false;
                integral = 0.0f;
                previous_error = 0.0f;

                ESP_LOGI(TAG, "Objetivo alcanzado. Posición: %.2f", position);
            } else {
                motor_apply(output);
            }

            previous_error = error;

            motor_state.setpoint = setpoint;
            motor_state.position = position;
            motor_state.error = error;
            motor_state.pwm = fabsf(output);
            motor_state.direction = (output >= 0.0f) ? 1 : -1;
            motor_state.moving = moving;

            xQueueOverwrite(MotorStateQueue, &motor_state);

            vTaskDelayUntil(&last_wake, period);
        }
    }
}

// ============================================================
// TASK: SAFETY_TASK - PRIORIDAD 2
// ============================================================

static void Safety_Task(void *pvParameters)
{
    motor_state_t state;
    float last_position = 0.0f;
    int block_counter = 0;

    while (1) {
        if (xQueueReceive(MotorStateQueue, &state, portMAX_DELAY) == pdTRUE) {
            float delta = fabsf(state.position - last_position);

            if (delta > 180.0f) {
                delta = 360.0f - delta;
            }

            if (state.moving &&
                state.pwm > BLOCK_PWM_MIN &&
                fabsf(state.error) > BLOCK_ERROR_MIN &&
                delta < BLOCK_DELTA_MIN) {

                block_counter++;
            } else {
                block_counter = 0;
            }

            if (block_counter >= BLOCK_COUNTER_LIMIT) {
                control_cmd_t cmd = {
                    .type = CMD_BLOCK_DETECTED,
                    .value = 0.0f
                };

                xQueueSend(ControlQueue, &cmd, 0);
                block_counter = 0;

                ESP_LOGW(TAG, "Safety_Task: posible bloqueo detectado");
            }

            last_position = state.position;
        }
    }
}

// ============================================================
// TASK: UART_COMMAND_TASK - PRIORIDAD 2
// ============================================================

static void UART_Command_Task(void *pvParameters)
{
    uint8_t data[UART_BUF_SIZE];

    while (1) {
        int len = uart_read_bytes(UART_PORT, data, UART_BUF_SIZE - 1, portMAX_DELAY);

        if (len > 0) {
            data[len] = '\0';

            ESP_LOGI(TAG, "UART RX: %s", (char *)data);

            control_cmd_t control_cmd;
            config_msg_t config_msg;
            display_msg_t display_msg;

            float value;

            if (sscanf((char *)data, "SET ANGLE %f", &value) == 1) {
                value = normalize_angle(value);

                // UART -> PID
                control_cmd.type = CMD_SET_ANGLE;
                control_cmd.value = value;
                xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

                // UART -> Storage
                g_config.setpoint = value;
                config_msg.type = CONFIG_SAVE_ALL;
                config_msg.config = g_config;
                xQueueSend(ConfigQueue, &config_msg, portMAX_DELAY);

                // UART -> Display
                display_msg.type = DISPLAY_SHOW_SETPOINT;
                display_msg.value = value;
                snprintf(display_msg.text, sizeof(display_msg.text), "Angle %.1f", value);
                xQueueSend(DisplayQueue, &display_msg, 0);
            }

            else if (sscanf((char *)data, "SET KP %f", &value) == 1) {
                control_cmd.type = CMD_SET_KP;
                control_cmd.value = value;
                xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

                g_config.kp = value;
                config_msg.type = CONFIG_SAVE_ALL;
                config_msg.config = g_config;
                xQueueSend(ConfigQueue, &config_msg, portMAX_DELAY);

                display_msg.type = DISPLAY_SHOW_MESSAGE;
                snprintf(display_msg.text, sizeof(display_msg.text), "KP %.2f", value);
                xQueueSend(DisplayQueue, &display_msg, 0);
            }

            else if (sscanf((char *)data, "SET KI %f", &value) == 1) {
                control_cmd.type = CMD_SET_KI;
                control_cmd.value = value;
                xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

                g_config.ki = value;
                config_msg.type = CONFIG_SAVE_ALL;
                config_msg.config = g_config;
                xQueueSend(ConfigQueue, &config_msg, portMAX_DELAY);

                display_msg.type = DISPLAY_SHOW_MESSAGE;
                snprintf(display_msg.text, sizeof(display_msg.text), "KI %.3f", value);
                xQueueSend(DisplayQueue, &display_msg, 0);
            }

            else if (sscanf((char *)data, "SET KD %f", &value) == 1) {
                control_cmd.type = CMD_SET_KD;
                control_cmd.value = value;
                xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

                g_config.kd = value;
                config_msg.type = CONFIG_SAVE_ALL;
                config_msg.config = g_config;
                xQueueSend(ConfigQueue, &config_msg, portMAX_DELAY);

                display_msg.type = DISPLAY_SHOW_MESSAGE;
                snprintf(display_msg.text, sizeof(display_msg.text), "KD %.2f", value);
                xQueueSend(DisplayQueue, &display_msg, 0);
            }

            else if (strstr((char *)data, "SET PROFILE ESCALON") != NULL ||
                     strstr((char *)data, "SET PROFILE FAST") != NULL) {
                control_cmd.type = CMD_SET_PROFILE;
                control_cmd.value = PROFILE_ESCALON;
                xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

                g_config.profile = PROFILE_ESCALON;
                config_msg.type = CONFIG_SAVE_ALL;
                config_msg.config = g_config;
                xQueueSend(ConfigQueue, &config_msg, portMAX_DELAY);

                display_msg.type = DISPLAY_SHOW_PROFILE;
                display_msg.value = PROFILE_ESCALON;
                snprintf(display_msg.text, sizeof(display_msg.text), "ESCALON");
                xQueueSend(DisplayQueue, &display_msg, 0);
            }

            else if (strstr((char *)data, "SET PROFILE RAMPA") != NULL ||
                     strstr((char *)data, "SET PROFILE SMOOTH") != NULL) {
                control_cmd.type = CMD_SET_PROFILE;
                control_cmd.value = PROFILE_RAMPA;
                xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

                g_config.profile = PROFILE_RAMPA;
                config_msg.type = CONFIG_SAVE_ALL;
                config_msg.config = g_config;
                xQueueSend(ConfigQueue, &config_msg, portMAX_DELAY);

                display_msg.type = DISPLAY_SHOW_PROFILE;
                display_msg.value = PROFILE_RAMPA;
                snprintf(display_msg.text, sizeof(display_msg.text), "RAMPA");
                xQueueSend(DisplayQueue, &display_msg, 0);
            }

            else if (strstr((char *)data, "SAVE") != NULL) {
                config_msg.type = CONFIG_SAVE_ALL;
                config_msg.config = g_config;
                xQueueSend(ConfigQueue, &config_msg, portMAX_DELAY);

                display_msg.type = DISPLAY_SHOW_MESSAGE;
                snprintf(display_msg.text, sizeof(display_msg.text), "Config saved");
                xQueueSend(DisplayQueue, &display_msg, 0);
            }

            else if (strstr((char *)data, "STOP") != NULL) {
                control_cmd.type = CMD_STOP;
                control_cmd.value = 0.0f;
                xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

                display_msg.type = DISPLAY_SHOW_MESSAGE;
                snprintf(display_msg.text, sizeof(display_msg.text), "STOP");
                xQueueSend(DisplayQueue, &display_msg, 0);
            }

            else {
                display_msg.type = DISPLAY_SHOW_MESSAGE;
                snprintf(display_msg.text, sizeof(display_msg.text), "CMD invalid");
                xQueueSend(DisplayQueue, &display_msg, 0);

                ESP_LOGW(TAG, "Comando UART no reconocido");
            }
        }
    }
}

// ============================================================
// TASK: BUTTON_HANDLER_TASK - PRIORIDAD 1
// ============================================================

static void ButtonHandler_Task(void *pvParameters)
{
    ui_menu_t menu = MENU_ANGLE;
    float selected_angle = g_config.setpoint;
    float selected_kp = g_config.kp;
    float selected_ki = g_config.ki;
    float selected_kd = g_config.kd;
    movement_profile_t selected_profile = g_config.profile;

    display_msg_t display_msg;
    control_cmd_t control_cmd;
    config_msg_t config_msg;

    (void)pvParameters;

    display_msg.type = DISPLAY_SHOW_SETPOINT;
    display_msg.value = selected_angle;
    snprintf(display_msg.text, sizeof(display_msg.text), "OK mueve");
    xQueueSend(DisplayQueue, &display_msg, 0);

    while (1) {
        if (xSemaphoreTake(sem_btn_menu, 0) == pdTRUE) {
            menu = (ui_menu_t)((menu + 1) % MENU_COUNT);

            switch (menu) {
                case MENU_ANGLE:
                    display_msg.type = DISPLAY_SHOW_SETPOINT;
                    display_msg.value = selected_angle;
                    snprintf(display_msg.text, sizeof(display_msg.text), "OK mueve");
                    break;

                case MENU_KP:
                    display_msg.type = DISPLAY_SHOW_KP;
                    display_msg.value = selected_kp;
                    break;

                case MENU_KI:
                    display_msg.type = DISPLAY_SHOW_KI;
                    display_msg.value = selected_ki;
                    break;

                case MENU_KD:
                    display_msg.type = DISPLAY_SHOW_KD;
                    display_msg.value = selected_kd;
                    break;

                case MENU_PROFILE:
                    display_msg.type = DISPLAY_SHOW_PROFILE;
                    display_msg.value = (float)selected_profile;
                    snprintf(display_msg.text, sizeof(display_msg.text), "%s",
                             selected_profile == PROFILE_ESCALON ? "ESCALON" : "RAMPA");
                    break;

                default:
                    display_msg.type = DISPLAY_SHOW_MENU;
                    break;
            }

            xQueueSend(DisplayQueue, &display_msg, 0);
            vTaskDelay(pdMS_TO_TICKS(150));
        }

        BaseType_t plus_pressed = xSemaphoreTake(sem_btn_plus, 0);
        BaseType_t minus_pressed = xSemaphoreTake(sem_btn_minus, 0);

        if (plus_pressed == pdTRUE || minus_pressed == pdTRUE) {
            int direction = plus_pressed == pdTRUE ? 1 : -1;

            switch (menu) {
                case MENU_ANGLE:
                    selected_angle += 5.0f * direction;
                    selected_angle = normalize_angle(selected_angle);
                    display_msg.type = DISPLAY_SHOW_SETPOINT;
                    display_msg.value = selected_angle;
                    snprintf(display_msg.text, sizeof(display_msg.text), "OK mueve");
                    break;

                case MENU_KP:
                    selected_kp += 0.1f * direction;
                    if (selected_kp < 0.0f) {
                        selected_kp = 0.0f;
                    }
                    g_config.kp = selected_kp;
                    control_cmd.type = CMD_SET_KP;
                    control_cmd.value = selected_kp;
                    xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);
                    config_msg.type = CONFIG_SAVE_ALL;
                    config_msg.config = g_config;
                    xQueueSend(ConfigQueue, &config_msg, 0);
                    display_msg.type = DISPLAY_SHOW_KP;
                    display_msg.value = selected_kp;
                    break;

                case MENU_KI:
                    selected_ki += 0.01f * direction;
                    if (selected_ki < 0.0f) {
                        selected_ki = 0.0f;
                    }
                    g_config.ki = selected_ki;
                    control_cmd.type = CMD_SET_KI;
                    control_cmd.value = selected_ki;
                    xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);
                    config_msg.type = CONFIG_SAVE_ALL;
                    config_msg.config = g_config;
                    xQueueSend(ConfigQueue, &config_msg, 0);
                    display_msg.type = DISPLAY_SHOW_KI;
                    display_msg.value = selected_ki;
                    break;

                case MENU_KD:
                    selected_kd += 0.01f * direction;
                    if (selected_kd < 0.0f) {
                        selected_kd = 0.0f;
                    }
                    g_config.kd = selected_kd;
                    control_cmd.type = CMD_SET_KD;
                    control_cmd.value = selected_kd;
                    xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);
                    config_msg.type = CONFIG_SAVE_ALL;
                    config_msg.config = g_config;
                    xQueueSend(ConfigQueue, &config_msg, 0);
                    display_msg.type = DISPLAY_SHOW_KD;
                    display_msg.value = selected_kd;
                    break;

                case MENU_PROFILE:
                    selected_profile = (selected_profile == PROFILE_ESCALON) ? PROFILE_RAMPA : PROFILE_ESCALON;
                    g_config.profile = selected_profile;
                    control_cmd.type = CMD_SET_PROFILE;
                    control_cmd.value = (float)selected_profile;
                    xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);
                    config_msg.type = CONFIG_SAVE_ALL;
                    config_msg.config = g_config;
                    xQueueSend(ConfigQueue, &config_msg, 0);
                    display_msg.type = DISPLAY_SHOW_PROFILE;
                    display_msg.value = (float)selected_profile;
                    snprintf(display_msg.text, sizeof(display_msg.text), "%s",
                             selected_profile == PROFILE_ESCALON ? "ESCALON" : "RAMPA");
                    break;

                default:
                    break;
            }

            xQueueSend(DisplayQueue, &display_msg, 0);
            vTaskDelay(pdMS_TO_TICKS(150));
        }

        if (xSemaphoreTake(sem_btn_ok, 0) == pdTRUE) {
            if (menu == MENU_ANGLE) {
                control_cmd.type = CMD_SET_PROFILE;
                control_cmd.value = (float)selected_profile;
                xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

                control_cmd.type = CMD_SET_ANGLE;
                control_cmd.value = selected_angle;
                xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

                g_config.setpoint = selected_angle;
                g_config.kp = selected_kp;
                g_config.ki = selected_ki;
                g_config.kd = selected_kd;
                g_config.profile = selected_profile;

                config_msg.type = CONFIG_SAVE_ALL;
                config_msg.config = g_config;
                xQueueSend(ConfigQueue, &config_msg, 0);

                display_msg.type = DISPLAY_SHOW_MESSAGE;
                snprintf(display_msg.text, sizeof(display_msg.text), "Moviendo %.1f", selected_angle);
                xQueueSend(DisplayQueue, &display_msg, 0);
            }

            vTaskDelay(pdMS_TO_TICKS(150));
        }

        vTaskDelay(pdMS_TO_TICKS(20));
        continue;

        if (xSemaphoreTake(sem_btn_ok, 0) == pdTRUE) {
            // Botón OK -> PID
            control_cmd.type = CMD_SET_PROFILE;
            control_cmd.value = (float)selected_profile;
            xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

            control_cmd.type = CMD_SET_ANGLE;
            control_cmd.value = selected_angle;
            xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);

            // Botón OK -> Storage
            g_config.setpoint = selected_angle;
            g_config.profile = selected_profile;

            config_msg.type = CONFIG_SAVE_ALL;
            config_msg.config = g_config;
            xQueueSend(ConfigQueue, &config_msg, 0);

            // Botón OK -> Display
            display_msg.type = DISPLAY_SHOW_MESSAGE;
            snprintf(display_msg.text, sizeof(display_msg.text), "Moving %.1f", selected_angle);
            xQueueSend(DisplayQueue, &display_msg, 0);

            vTaskDelay(pdMS_TO_TICKS(150));
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================================
// TASK: DISPLAY_TASK - PRIORIDAD 1
// ============================================================

static void Display_Task(void *pvParameters)
{
    display_msg_t msg;
    i2c_request_t req;

    (void)pvParameters;

    memset(&req, 0, sizeof(req));
    req.type = I2C_REQ_LCD_INIT;
    xQueueSend(I2C_TXQueue, &req, portMAX_DELAY);

    memset(&req, 0, sizeof(req));
    req.type = I2C_REQ_LCD_PRINT;
    snprintf(req.lcd_line1, sizeof(req.lcd_line1), "Menu Angulo");
    snprintf(req.lcd_line2, sizeof(req.lcd_line2), "%.1f deg OK", g_config.setpoint);
    xQueueSend(I2C_TXQueue, &req, portMAX_DELAY);

    while (1) {
        if (xQueueReceive(DisplayQueue, &msg, portMAX_DELAY) == pdTRUE) {
            memset(&req, 0, sizeof(req));
            req.type = I2C_REQ_LCD_PRINT;

            switch (msg.type) {
                case DISPLAY_SHOW_SETPOINT:
                    snprintf(req.lcd_line1, sizeof(req.lcd_line1), "Menu Angulo");
                    snprintf(req.lcd_line2, sizeof(req.lcd_line2), "%.1f deg OK", msg.value);
                    break;

                case DISPLAY_SHOW_KP:
                    snprintf(req.lcd_line1, sizeof(req.lcd_line1), "Menu Kp");
                    snprintf(req.lcd_line2, sizeof(req.lcd_line2), "Kp %.2f", msg.value);
                    break;

                case DISPLAY_SHOW_KI:
                    snprintf(req.lcd_line1, sizeof(req.lcd_line1), "Menu Ki");
                    snprintf(req.lcd_line2, sizeof(req.lcd_line2), "Ki %.3f", msg.value);
                    break;

                case DISPLAY_SHOW_KD:
                    snprintf(req.lcd_line1, sizeof(req.lcd_line1), "Menu Kd");
                    snprintf(req.lcd_line2, sizeof(req.lcd_line2), "Kd %.3f", msg.value);
                    break;

                case DISPLAY_SHOW_PROFILE:
                    snprintf(req.lcd_line1, sizeof(req.lcd_line1), "Menu Perfil");
                    snprintf(req.lcd_line2, sizeof(req.lcd_line2), "%s",
                             (movement_profile_t)((int)msg.value) == PROFILE_ESCALON ? "ESCALON" : "RAMPA");
                    break;

                case DISPLAY_SHOW_MESSAGE:
                    snprintf(req.lcd_line1, sizeof(req.lcd_line1), "Mensaje");
                    snprintf(req.lcd_line2, sizeof(req.lcd_line2), "%.*s", LCD_COLS, msg.text);
                    break;

                case DISPLAY_SHOW_MENU:
                default:
                    snprintf(req.lcd_line1, sizeof(req.lcd_line1), "TD3 PID");
                    snprintf(req.lcd_line2, sizeof(req.lcd_line2), "Menu");
                    break;
            }

            // Display solo escribe por I2C_TXQueue.
            // No recibe I2C_RXQueue.
            xQueueSend(I2C_TXQueue, &req, portMAX_DELAY);
        }
    }
}

// ============================================================
// TASK: STORAGE_TASK - PRIORIDAD 1
// ============================================================

static void Storage_Task(void *pvParameters)
{
    system_config_t cfg;
    config_msg_t msg;

    // Al iniciar, intenta recuperar configuración guardada.
    if (load_config_from_nvs(&cfg)) {
        g_config = cfg;

        ESP_LOGI(TAG, "Configuración recuperada desde NVS");
        ESP_LOGI(TAG, "Setpoint: %.2f | Kp: %.2f | Ki: %.3f | Kd: %.2f | Profile: %d",
                 g_config.setpoint,
                 g_config.kp,
                 g_config.ki,
                 g_config.kd,
                 g_config.profile);

        // Storage -> PID por ControlQueue
        control_cmd_t control_cmd;

        control_cmd.type = CMD_LOAD_CONFIG;
        control_cmd.value = 0.0f;
        xQueueSend(ControlQueue, &control_cmd, 0);

        control_cmd.type = CMD_SET_KP;
        control_cmd.value = g_config.kp;
        xQueueSend(ControlQueue, &control_cmd, 0);

        control_cmd.type = CMD_SET_KI;
        control_cmd.value = g_config.ki;
        xQueueSend(ControlQueue, &control_cmd, 0);

        control_cmd.type = CMD_SET_KD;
        control_cmd.value = g_config.kd;
        xQueueSend(ControlQueue, &control_cmd, 0);

        control_cmd.type = CMD_SET_PROFILE;
        control_cmd.value = (float)g_config.profile;
        xQueueSend(ControlQueue, &control_cmd, 0);

        // No mando CMD_SET_ANGLE al inicio para evitar que el motor se mueva solo al reiniciar.
        // El ángulo queda recuperado como configuración y se puede mostrar en el display.

        display_msg_t display_msg;
        display_msg.type = DISPLAY_SHOW_SETPOINT;
        display_msg.value = g_config.setpoint;
        snprintf(display_msg.text, sizeof(display_msg.text), "Set %.1f", g_config.setpoint);
        xQueueSend(DisplayQueue, &display_msg, 0);
    } else {
        ESP_LOGW(TAG, "No había configuración guardada. Se usan valores por defecto.");
    }

    while (1) {
        if (xQueueReceive(ConfigQueue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.type) {
                case CONFIG_SAVE_ALL:
                    g_config = msg.config;
                    save_config_to_nvs(&g_config);
                    break;

                case CONFIG_LOAD_ALL:
                    if (load_config_from_nvs(&cfg)) {
                        g_config = cfg;
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

// ============================================================
// INICIALIZACIÓN GPIO
// ============================================================

static void init_gpio(void)
{
    gpio_config_t motor_conf = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_IN1) |
                        (1ULL << PIN_MOTOR_IN2) |
                        (1ULL << PIN_AS5600_DIR),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&motor_conf));

    gpio_set_level(PIN_MOTOR_IN1, 0);
    gpio_set_level(PIN_MOTOR_IN2, 0);

    // DIR del AS5600: 0 o 1 según sentido deseado.
    gpio_set_level(PIN_AS5600_DIR, 0);

    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << PIN_BTN_PLUS) |
                        (1ULL << PIN_BTN_MINUS) |
                        (1ULL << PIN_BTN_MENU) |
                        (1ULL << PIN_BTN_OK),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };

    ESP_ERROR_CHECK(gpio_config(&btn_conf));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BTN_PLUS, gpio_button_isr_handler, (void *)PIN_BTN_PLUS));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BTN_MINUS, gpio_button_isr_handler, (void *)PIN_BTN_MINUS));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BTN_MENU, gpio_button_isr_handler, (void *)PIN_BTN_MENU));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BTN_OK, gpio_button_isr_handler, (void *)PIN_BTN_OK));
}

// ============================================================
// INICIALIZACIÓN I2C
// ============================================================

static void init_i2c(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ
    };

    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0));
}

// ============================================================
// INICIALIZACIÓN PWM
// ============================================================

static void init_pwm(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE_USED,
        .timer_num = LEDC_TIMER_USED,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE_USED,
        .channel = LEDC_CHANNEL_USED,
        .timer_sel = LEDC_TIMER_USED,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PIN_MOTOR_PWM,
        .duty = 0,
        .hpoint = 0
    };

    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

// ============================================================
// INICIALIZACIÓN UART
// ============================================================

static void init_uart(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, PIN_UART_TX, PIN_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// ============================================================
// APP_MAIN
// ============================================================

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    init_gpio();
    init_i2c();
    init_pwm();
    init_uart();

    ControlQueue = xQueueCreate(10, sizeof(control_cmd_t));
    I2C_TXQueue = xQueueCreate(10, sizeof(i2c_request_t));
    I2C_RXQueue = xQueueCreate(10, sizeof(i2c_response_t));

    angleQueue = xQueueCreate(1, sizeof(float));
    MotorStateQueue = xQueueCreate(1, sizeof(motor_state_t));

    DisplayQueue = xQueueCreate(10, sizeof(display_msg_t));
    ConfigQueue = xQueueCreate(5, sizeof(config_msg_t));

    sem_btn_plus = xSemaphoreCreateBinary();
    sem_btn_minus = xSemaphoreCreateBinary();
    sem_btn_menu = xSemaphoreCreateBinary();
    sem_btn_ok = xSemaphoreCreateBinary();

    if (!ControlQueue ||
        !I2C_TXQueue ||
        !I2C_RXQueue ||
        !angleQueue ||
        !MotorStateQueue ||
        !DisplayQueue ||
        !ConfigQueue ||
        !sem_btn_plus ||
        !sem_btn_minus ||
        !sem_btn_menu ||
        !sem_btn_ok) {

        ESP_LOGE(TAG, "Error creando colas o semáforos");
        return;
    }

    // Prioridad 3
    xTaskCreate(PID_Task, "PID_Task", 4096, NULL, 3, NULL);
    xTaskCreate(I2C_Manager_Task, "I2C_Manager_Task", 4096, NULL, 3, NULL);
    xTaskCreate(AS5600_Reader_Task, "AS5600_Reader_Task", 4096, NULL, 3, NULL);

    // Prioridad 2
    xTaskCreate(Safety_Task, "Safety_Task", 4096, NULL, 2, NULL);
    xTaskCreate(UART_Command_Task, "UART_Command_Task", 4096, NULL, 2, NULL);

    // Prioridad 1
    xTaskCreate(ButtonHandler_Task, "ButtonHandler_Task", 4096, NULL, 1, NULL);
    xTaskCreate(Display_Task, "Display_Task", 4096, NULL, 1, NULL);
    xTaskCreate(Storage_Task, "Storage_Task", 4096, NULL, 1, NULL);

    ESP_LOGI(TAG, "Sistema iniciado");
}
