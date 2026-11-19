#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/uart.h"

#include "app_config.h"
#include "as5600_sensor.h"
#include "angle_utils.h"
#include "lcd_display.h"
#include "motor_driver.h"
#include "uart_protocol.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"

// ============================================================
// TAG
// ============================================================

static const char *TAG = "TD3_PID";

// ============================================================
// TIPOS
// ============================================================

typedef enum {
    PROFILE_ESCALON = 0,
    PROFILE_RAMPA
} movement_profile_t;

// Comandos que cualquier tarea puede enviar al PID por ControlQueue.
// El PID es la unica tarea que decide finalmente que hacer con el motor.
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
    TaskHandle_t completion_task;
} control_cmd_t;

// Solicitudes al bus I2C. La tarea I2C_Manager_Task centraliza el bus para
// evitar que AS5600 y LCD intenten usar I2C al mismo tiempo.
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

// Estado resumido del motor que el PID publica para Safety_Task.
// Safety_Task no maneja el motor directamente: solo observa y avisa al PID.
typedef struct {
    float setpoint;
    float position;
    float error;
    float pwm;
    int direction;
    bool moving;
} motor_state_t;

// Mensajes logicos para el display. Display_Task traduce estos mensajes a
// texto de 16x2 y despues pide la escritura fisica por I2C_TXQueue.
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

// Menus disponibles en la interfaz de botones.
typedef enum {
    MENU_ANGLE = 0,
    MENU_KP,
    MENU_KI,
    MENU_KD,
    MENU_PROFILE,
    MENU_COUNT
} ui_menu_t;

typedef enum {
    BUTTON_PLUS = 0,
    BUTTON_MINUS,
    BUTTON_MENU,
    BUTTON_OK
} button_id_t;

typedef struct {
    button_id_t button;
    TickType_t tick;
} button_event_t;

// Configuracion persistente. Se guarda como un blob completo en NVS para que
// al reiniciar se recuperen setpoint, constantes PID y perfil.
typedef struct {
    float setpoint;
    float kp;
    float ki;
    float kd;
    movement_profile_t profile;
} system_config_t;

typedef struct {
    system_config_t config;
    motor_state_t motor;
    bool position_valid;
} telemetry_t;

typedef enum {
    CONFIG_SAVE_ALL = 0,
    CONFIG_LOAD_ALL
} config_cmd_type_t;

typedef struct {
    config_cmd_type_t type;
    system_config_t config;
} config_msg_t;

// ============================================================
// COLAS
// ============================================================

// Las colas separan responsabilidades:
// ControlQueue lleva comandos hacia PID_Task.
// I2C_TXQueue/I2C_RXQueue serializan el uso del bus I2C.
// angleQueue conserva la ultima posicion medida por el AS5600.
// MotorStateQueue permite que Safety_Task observe el movimiento.
// DisplayQueue desacopla mensajes de pantalla del driver LCD.
// ConfigQueue concentra escrituras/lecturas de NVS en Storage_Task.
// ButtonEventQueue lleva flancos de botones desde la ISR hacia ButtonHandler_Task.
static QueueHandle_t ControlQueue;
static QueueHandle_t I2C_TXQueue;
static QueueHandle_t I2C_RXQueue;
static QueueHandle_t angleQueue;
static QueueHandle_t MotorStateQueue;
static QueueHandle_t TelemetryQueue;
static QueueHandle_t DisplayQueue;
static QueueHandle_t ConfigQueue;
static QueueHandle_t ButtonEventQueue;

// Tiempos usados por la ISR para antirrebote por software.
static volatile TickType_t last_btn_plus_tick;
static volatile TickType_t last_btn_minus_tick;
static volatile TickType_t last_btn_menu_tick;
static volatile TickType_t last_btn_ok_tick;

// ============================================================
// CONFIGURACIÓN GLOBAL
// ============================================================

// Configuracion solicitada por la UI. La configuracion activa se publica desde PID.
static system_config_t g_config = {
    .setpoint = 0.0f,
    .kp = 6.0f,
    .ki = 0.0f,
    .kd = 0.5f,
    .profile = PROFILE_ESCALON
};

// ============================================================
// FUNCIONES AUXILIARES
// ============================================================

static void drain_button_queue(void)
{
    button_event_t discarded;

    // Descartar eventos acumulados evita que un rebote viejo se interprete
    // como una nueva accion de usuario.
    while (xQueueReceive(ButtonEventQueue, &discarded, 0) == pdTRUE) {
    }
}


static float step_angle_by_5(float angle, int direction)
{
    float normalized = angle_normalize(angle);
    float stepped;

    if (direction > 0) {
        stepped = floorf(normalized / 5.0f) * 5.0f + 5.0f;
    } else {
        stepped = ceilf(normalized / 5.0f) * 5.0f - 5.0f;
    }

    return angle_normalize(stepped);
}

static float angle_error_for_direction(float setpoint, float position, int direction)
{
    if (direction > 0) {
        return angle_normalize(setpoint - position);
    }

    if (direction < 0) {
        return -angle_normalize(position - setpoint);
    }

    return angle_shortest_error(setpoint, position);
}



static void uart_send_line(const char *text)
{
#if UART_USE_CONSOLE
    printf("%s\r\n", text);
    fflush(stdout);
#else
    uart_write_bytes(UART_PORT, text, strlen(text));
    uart_write_bytes(UART_PORT, "\r\n", 2);
#endif
}

static void publish_telemetry(float setpoint, float kp, float ki, float kd,
                              movement_profile_t profile, bool moving,
                              const motor_state_t *motor)
{
    telemetry_t snapshot = {
        .config = {setpoint, kp, ki, kd, profile},
        .motor = *motor
    };
    snapshot.position_valid = xQueuePeek(angleQueue, &snapshot.motor.position, 0) == pdTRUE;
    snapshot.motor.setpoint = setpoint;
    snapshot.motor.error = angle_shortest_error(setpoint, snapshot.motor.position);
    snapshot.motor.moving = moving;
    if (!moving) {
        snapshot.motor.pwm = 0.0f;
        snapshot.motor.direction = 0;
    } else {
        snapshot.motor.pwm = fminf(snapshot.motor.pwm, PWM_MAX);
    }
    xQueueOverwrite(TelemetryQueue, &snapshot);
}

// ============================================================
// NVS
// ============================================================

static void save_config_to_nvs(const system_config_t *cfg)
{
    // La configuracion se guarda como un unico blob para mantener consistentes
    // setpoint, Kp, Ki, Kd y perfil.
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
    // Si no existe configuracion valida, el sistema arranca con defaults.
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
    TickType_t now = xTaskGetTickCountFromISR();
    TickType_t debounce_ticks = pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS);
    volatile TickType_t *last_tick = NULL;
    button_event_t event;
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (pin == PIN_BTN_PLUS) {
        event.button = BUTTON_PLUS;
        last_tick = &last_btn_plus_tick;
    } else if (pin == PIN_BTN_MINUS) {
        event.button = BUTTON_MINUS;
        last_tick = &last_btn_minus_tick;
    } else if (pin == PIN_BTN_MENU) {
        event.button = BUTTON_MENU;
        last_tick = &last_btn_menu_tick;
    } else if (pin == PIN_BTN_OK) {
        event.button = BUTTON_OK;
        last_tick = &last_btn_ok_tick;
    } else {
        return;
    }

    event.tick = now;

    if (last_tick != NULL &&
        (*last_tick == 0 || (now - *last_tick) >= debounce_ticks)) {
        // La ISR solo empaqueta que boton genero el flanco. La logica de menu
        // queda en ButtonHandler_Task, fuera de la interrupcion.
        if (ButtonEventQueue == NULL) {
            return;
        }

        *last_tick = now;
        xQueueSendFromISR(ButtonEventQueue, &event, &higher_priority_task_woken);
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
                    // Lectura sincronica del sensor. La respuesta vuelve por
                    // I2C_RXQueue porque solo AS5600_Reader_Task espera raw data.
                    if (as5600_read_raw(&resp.as5600_raw) == ESP_OK) {
                        resp.ok = true;
                    } else {
                        resp.ok = false;
                    }

                    // I2C_RXQueue solo se usa para responder al AS5600_Reader_Task.
                    xQueueSend(I2C_RXQueue, &resp, portMAX_DELAY);
                    break;

                case I2C_REQ_LCD_INIT: {
                    // El LCD tambien usa el mismo bus I2C, por eso se inicializa
                    // desde esta tarea y no desde Display_Task directamente.
                    esp_err_t ret = lcd_display_init();

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
                        // Display_Task ya formateo las lineas. Aca solo se
                        // ejecuta la operacion fisica sobre el LCD.
                        esp_err_t ret = lcd_display_write_screen(req.lcd_line1, req.lcd_line2);

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
#if AS5600_LOG_ENABLE
    TickType_t last_log = 0;
#endif

    while (1) {
        // No toca I2C directo: pide la lectura al manager para respetar el
        // arbitraje del bus compartido con el LCD.
        xQueueSend(I2C_TXQueue, &req, portMAX_DELAY);

        if (xQueueReceive(I2C_RXQueue, &resp, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (resp.ok) {
                angle = angle_raw_to_degrees(resp.as5600_raw);
                angle = angle_normalize(angle);

                // Cola de longitud 1: siempre queda la última posición.
                xQueueOverwrite(angleQueue, &angle);

#if AS5600_LOG_ENABLE
                TickType_t now = xTaskGetTickCount();

                if ((now - last_log) >= pdMS_TO_TICKS(500)) {
                    ESP_LOGI(TAG, "AS5600 raw=%u angle=%.2f deg", (unsigned)resp.as5600_raw, angle);
                    last_log = now;
                }
#endif
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
    motor_state_t motor_state = {0};

    float setpoint = g_config.setpoint;
    float kp = g_config.kp;
    float ki = g_config.ki;
    float kd = g_config.kd;
    movement_profile_t profile = g_config.profile;

    float position = 0.0f;
    float error = 0.0f;
    float target_error = 0.0f;
    float control_setpoint = setpoint;
    float previous_error = 0.0f;
    float integral = 0.0f;
    float derivative = 0.0f;
    float output = 0.0f;
    TickType_t last_pid_log = 0;
    TickType_t start_boost_until = 0;
    TickType_t target_verify_until = 0;
    TickType_t fine_pulse_until = 0;
    TickType_t fine_brake_until = 0;
    int forced_direction = 0;

    bool moving = false;
    bool hold_brake = false;
    bool verifying_target = false;
    bool ramp_initialized = false;

    while (1) {
        if (!moving) {
            // En reposo el PID no calcula nada: espera comandos. Si se alcanzo
            // un objetivo, mantiene freno electrico para que el eje no se vaya.
            if (hold_brake) {
                motor_brake();
            } else {
                motor_stop();
            }

            motor_state.pwm = 0.0f;
            motor_state.direction = 0;
            publish_telemetry(setpoint, kp, ki, kd, profile, moving, &motor_state);

            // Refresca la monitorizacion en reposo, sin ejecutar el PID.
            if (xQueueReceive(ControlQueue, &cmd, period) == pdTRUE) {
                switch (cmd.type) {
                    case CMD_SET_ANGLE: {
                        float current_position = 0.0f;

                        // Nuevo objetivo: se reinician estados dinamicos del PID
                        // para no arrastrar integral/derivada de otro movimiento.
                        setpoint = angle_normalize(cmd.value);
                        control_setpoint = setpoint;
                        integral = 0.0f;
                        hold_brake = false;
                        verifying_target = false;
                        forced_direction = 0;
                        ramp_initialized = profile != PROFILE_RAMPA;
                        fine_pulse_until = 0;
                        fine_brake_until = 0;
                        if (xQueuePeek(angleQueue, &current_position, 0) == pdTRUE) {
                            if (profile == PROFILE_RAMPA) {
                                control_setpoint = current_position;
                                ramp_initialized = true;
                                previous_error = 0.0f;
                            } else {
                                ramp_initialized = true;
                                previous_error = angle_shortest_error(control_setpoint, current_position);
                            }
                        } else {
                            previous_error = 0.0f;
                        }
                        start_boost_until = xTaskGetTickCount() + pdMS_TO_TICKS(PWM_START_BOOST_MS);
                        moving = true;
                        ESP_LOGI(TAG, "Nuevo setpoint y movimiento: %.2f", setpoint);
                        break;
                    }

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

                    case CMD_SET_PROFILE: {
                        float current_position = 0.0f;

                        profile = (movement_profile_t)((int)cmd.value);
                        integral = 0.0f;
                        previous_error = 0.0f;

                        if (profile == PROFILE_ESCALON) {
                            control_setpoint = setpoint;
                            ramp_initialized = true;
                            if (xQueuePeek(angleQueue, &current_position, 0) == pdTRUE) {
                                previous_error = angle_shortest_error(control_setpoint, current_position);
                            }
                        } else if (xQueuePeek(angleQueue, &current_position, 0) == pdTRUE) {
                            control_setpoint = current_position;
                            ramp_initialized = true;
                        } else {
                            ramp_initialized = false;
                        }

                        ESP_LOGI(TAG, "Profile = %d", profile);
                        break;
                    }

                    case CMD_LOAD_CONFIG:
                        setpoint = g_config.setpoint;
                        kp = g_config.kp;
                        ki = g_config.ki;
                        kd = g_config.kd;
                        profile = g_config.profile;
                        control_setpoint = setpoint;
                        ramp_initialized = profile != PROFILE_RAMPA;
                        ESP_LOGI(TAG, "Config cargada en PID");
                        break;

                    case CMD_STOP:
                        motor_stop();
                        moving = false;
                        hold_brake = false;
                        verifying_target = false;
                        forced_direction = 0;
                        control_setpoint = setpoint;
                        ramp_initialized = false;
                        fine_pulse_until = 0;
                        fine_brake_until = 0;
                        break;

                    default:
                        break;
                }
                publish_telemetry(setpoint, kp, ki, kd, profile, moving, &motor_state);
                if (cmd.completion_task != NULL) {
                    xTaskNotifyGive(cmd.completion_task);
                }
            }
        } else {
            // Durante movimiento no se puede bloquear esperando ControlQueue.
            while (xQueueReceive(ControlQueue, &cmd, 0) == pdTRUE) {
                switch (cmd.type) {
                    case CMD_STOP:
                        moving = false;
                        hold_brake = false;
                        verifying_target = false;
                        forced_direction = 0;
                        control_setpoint = setpoint;
                        ramp_initialized = false;
                        fine_pulse_until = 0;
                        fine_brake_until = 0;
                        motor_stop();
                        motor_state.pwm = 0.0f;
                        motor_state.direction = 0;
                        ESP_LOGI(TAG, "STOP recibido");
                        break;

                    case CMD_SET_ANGLE: {
                        float new_setpoint = angle_normalize(cmd.value);
                        float current_position = 0.0f;

                        if (fabsf(angle_shortest_error(new_setpoint, setpoint)) < 0.01f) {
                            ESP_LOGI(TAG, "Setpoint duplicado ignorado: %.2f", new_setpoint);
                            break;
                        }

                        setpoint = new_setpoint;
                        control_setpoint = setpoint;
                        integral = 0.0f;
                        hold_brake = false;
                        verifying_target = false;
                        forced_direction = 0;
                        ramp_initialized = profile != PROFILE_RAMPA;
                        fine_pulse_until = 0;
                        fine_brake_until = 0;
                        if (xQueuePeek(angleQueue, &current_position, 0) == pdTRUE) {
                            if (profile == PROFILE_RAMPA) {
                                control_setpoint = current_position;
                                ramp_initialized = true;
                                previous_error = 0.0f;
                            } else {
                                ramp_initialized = true;
                                previous_error = angle_shortest_error(control_setpoint, current_position);
                            }
                        } else {
                            previous_error = 0.0f;
                        }
                        start_boost_until = xTaskGetTickCount() + pdMS_TO_TICKS(PWM_START_BOOST_MS);
                        ESP_LOGI(TAG, "Nuevo setpoint durante movimiento: %.2f", setpoint);
                        break;
                    }

                    case CMD_SET_KP:
                        kp = cmd.value;
                        break;

                    case CMD_SET_KI:
                        ki = cmd.value;
                        break;

                    case CMD_SET_KD:
                        kd = cmd.value;
                        break;

                    case CMD_SET_PROFILE: {
                        float current_position = 0.0f;

                        profile = (movement_profile_t)((int)cmd.value);
                        integral = 0.0f;
                        previous_error = 0.0f;

                        if (profile == PROFILE_ESCALON) {
                            control_setpoint = setpoint;
                            ramp_initialized = true;
                            if (xQueuePeek(angleQueue, &current_position, 0) == pdTRUE) {
                                previous_error = angle_shortest_error(control_setpoint, current_position);
                            }
                        } else if (xQueuePeek(angleQueue, &current_position, 0) == pdTRUE) {
                            control_setpoint = current_position;
                            ramp_initialized = true;
                        } else {
                            ramp_initialized = false;
                        }
                        break;
                    }

                    case CMD_BLOCK_DETECTED:
                        // Safety_Task detecto que hay PWM y error alto, pero el
                        // eje casi no cambia. Se fuerza el sentido contrario para
                        // intentar salir del bloqueo sin apagar el motor.
                        verifying_target = false;
                        if (forced_direction == 0) {
                            forced_direction = (error >= 0.0f) ? -1 : 1;
                        } else {
                            forced_direction = -forced_direction;
                        }
                        fine_pulse_until = 0;
                        fine_brake_until = 0;
                        integral = 0.0f;
                        previous_error = 0.0f;
                        start_boost_until = xTaskGetTickCount() + pdMS_TO_TICKS(PWM_START_BOOST_MS);
                        ESP_LOGW(TAG, "Bloqueo detectado. Nuevo sentido forzado: %d", forced_direction);
                        break;

                    case CMD_REVERSE:
                        output = -output;
                        motor_apply(output);
                        ESP_LOGW(TAG, "CMD_REVERSE recibido");
                        break;

                    case CMD_LOAD_CONFIG:
                        setpoint = g_config.setpoint;
                        kp = g_config.kp;
                        ki = g_config.ki;
                        kd = g_config.kd;
                        profile = g_config.profile;
                        control_setpoint = setpoint;
                        ramp_initialized = profile != PROFILE_RAMPA;
                        break;

                    default:
                        break;
                }
                publish_telemetry(setpoint, kp, ki, kd, profile, moving, &motor_state);
                if (cmd.completion_task != NULL) {
                    xTaskNotifyGive(cmd.completion_task);
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

            float dt = PID_PERIOD_MS / 1000.0f;
            target_error = angle_shortest_error(setpoint, position);

            if (forced_direction != 0) {
                error = angle_error_for_direction(setpoint, position, forced_direction);
            } else if (profile == PROFILE_RAMPA) {
                if (!ramp_initialized) {
                    control_setpoint = position;
                    ramp_initialized = true;
                    integral = 0.0f;
                    previous_error = 0.0f;
                }

                float remaining = angle_shortest_error(setpoint, control_setpoint);
                float max_ramp_step = RAMP_SPEED_DEG_S * dt;

                if (fabsf(remaining) <= max_ramp_step) {
                    control_setpoint = setpoint;
                } else {
                    control_setpoint = angle_normalize(
                        control_setpoint + ((remaining > 0.0f) ? max_ramp_step : -max_ramp_step)
                    );
                }

                error = angle_shortest_error(control_setpoint, position);
            } else {
                control_setpoint = setpoint;
                ramp_initialized = true;
                error = target_error;
            }

            if (forced_direction != 0 && fabsf(target_error) <= FORCED_DIRECTION_RELEASE_DEG) {
                // El bloqueo puede obligar a tomar el camino largo. Cuando ya
                // estamos cerca del objetivo, se libera esa restriccion para
                // evitar que el eje pase de largo y complete otra vuelta.
                forced_direction = 0;
                integral = 0.0f;
                fine_pulse_until = 0;
                fine_brake_until = 0;
                start_boost_until = 0;

                if (profile == PROFILE_RAMPA) {
                    control_setpoint = position;
                    ramp_initialized = true;
                    error = 0.0f;
                } else {
                    control_setpoint = setpoint;
                    ramp_initialized = true;
                    error = target_error;
                }

                previous_error = error;
                ESP_LOGI(TAG, "Liberando sentido forzado cerca del objetivo: err=%.2f", target_error);
            }
            if (verifying_target) {
                TickType_t now = xTaskGetTickCount();

                // Al entrar en tolerancia se frena y se espera un tiempo corto.
                // Asi no se declara "objetivo alcanzado" por una lectura aislada
                // mientras el eje todavia viene con inercia.
                motor_brake();
                output = 0.0f;
                integral = 0.0f;
                previous_error = error;

                if ((int32_t)(now - target_verify_until) >= 0) {
                    if (fabsf(target_error) <= ANGLE_ACCEPT_DEG) {
                        display_msg_t display_msg;

                        moving = false;
                        hold_brake = true;
                        verifying_target = false;
                        forced_direction = 0;
                        control_setpoint = setpoint;
                        ramp_initialized = false;
                        ESP_LOGI(TAG, "Objetivo alcanzado. Posicion: %.2f", position);
                        display_msg.type = DISPLAY_SHOW_MESSAGE;
                        display_msg.value = setpoint;
                        snprintf(display_msg.text, sizeof(display_msg.text), "OK %.1f deg", setpoint);
                        xQueueSend(DisplayQueue, &display_msg, 0);
                    } else {
                        verifying_target = false;
                        ESP_LOGI(TAG, "Objetivo se movio durante verificacion. Retomando PID: %.2f", position);
                    }
                }

                motor_state.setpoint = setpoint;
                motor_state.position = position;
                motor_state.error = target_error;
                motor_state.pwm = 0.0f;
                motor_state.direction = 0;
                motor_state.moving = moving;

                xQueueOverwrite(MotorStateQueue, &motor_state);
                publish_telemetry(setpoint, kp, ki, kd, profile, moving, &motor_state);

                vTaskDelayUntil(&last_wake, period);
                continue;
            }

            integral += error * dt;

            // Antiwindup simple: limita la integral para que no siga acumulando
            // error cuando el actuador ya esta saturado.
            if (integral > 100.0f) {
                integral = 100.0f;
            }

            if (integral < -100.0f) {
                integral = -100.0f;
            }

            derivative = (error - previous_error) / dt;

            output = kp * error + ki * integral + kd * derivative;


            if (fabsf(target_error) <= ANGLE_ACCEPT_DEG) {
                TickType_t now = xTaskGetTickCount();

                // Primera entrada al rango aceptado. Se congela el control y se
                // verifica estabilidad antes de avisar por display.
                motor_brake();
                integral = 0.0f;
                previous_error = error;
                output = 0.0f;

                if (!verifying_target) {
                    verifying_target = true;
                    target_verify_until = now + pdMS_TO_TICKS(TARGET_VERIFY_MS);
                    ESP_LOGI(TAG, "Objetivo en rango. Verificando posicion: %.2f", position);
                }
            } else {
                verifying_target = false;

                TickType_t control_now = xTaskGetTickCount();

                if (fabsf(target_error) <= FINE_CONTROL_ZONE_DEG) {
                    // Cerca del objetivo se usa un PWM menor que el de movimiento
                    // normal. Con mas tension de alimentacion, el mismo duty
                    // genera un empujon mas fuerte.
                    if (fine_brake_until != 0 &&
                        (int32_t)(control_now - fine_brake_until) < 0) {
                        output = 0.0f;
                        motor_brake();
                    } else {
                        fine_brake_until = 0;

                        if (fine_pulse_until == 0) {
                            fine_pulse_until = control_now + pdMS_TO_TICKS(FINE_PULSE_MS);
                        }

                        if ((int32_t)(control_now - fine_pulse_until) < 0) {
                            output = (target_error >= 0.0f) ? PWM_FINE_MIN : -PWM_FINE_MIN;
                            motor_apply(output);
                        } else {
                            fine_pulse_until = 0;
                            fine_brake_until = control_now + pdMS_TO_TICKS(FINE_BRAKE_MS);
                            output = 0.0f;
                            motor_brake();
                        }
                    }
                } else {
                    // Lejos del objetivo se usa control continuo con boost
                    // inicial para vencer inercia/rozamiento de arranque.
                    fine_pulse_until = 0;
                    fine_brake_until = 0;

                    float min_output = PWM_MOVE_MIN;

                    if (fabsf(output) < min_output) {
                        output = (output >= 0.0f) ? min_output : -min_output;
                    }

                    if (control_now < start_boost_until &&
                        fabsf(output) < PWM_START_BOOST) {
                        output = (output >= 0.0f) ? PWM_START_BOOST : -PWM_START_BOOST;
                    }

                    motor_apply(output);
                }
            }

            previous_error = error;

            motor_state.setpoint = setpoint;
            motor_state.position = position;
            motor_state.error = target_error;
            motor_state.pwm = fabsf(output);
            if (output > 0.0f) {
                motor_state.direction = 1;
            } else if (output < 0.0f) {
                motor_state.direction = -1;
            } else {
                motor_state.direction = 0;
            }
            motor_state.moving = moving;

            xQueueOverwrite(MotorStateQueue, &motor_state);
            publish_telemetry(setpoint, kp, ki, kd, profile, moving, &motor_state);

            TickType_t now = xTaskGetTickCount();
            if ((now - last_pid_log) >= pdMS_TO_TICKS(250)) {
                if (profile == PROFILE_RAMPA && forced_direction == 0) {
                    ESP_LOGI(TAG,
                             "PID target=%.2f ramp_sp=%.2f pos=%.2f pid_err=%.2f target_err=%.2f out=%.2f dir=%d",
                             setpoint,
                             control_setpoint,
                             position,
                             error,
                             target_error,
                             output,
                             motor_state.direction);
                } else {
                    ESP_LOGI(TAG, "PID sp=%.2f pos=%.2f err=%.2f out=%.2f dir=%d",
                             setpoint,
                             position,
                             error,
                             output,
                             motor_state.direction);
                }
                last_pid_log = now;
            }

            vTaskDelayUntil(&last_wake, period);
        }
    }
}

// ============================================================
// TASK: SAFETY_TASK - PRIORIDAD 2
// ============================================================

static void Safety_Task(void *pvParameters)
{
    // Supervisa el movimiento publicado por PID_Task. Para evitar falsos
    // bloqueos no alcanza con una lectura: se exige falta de progreso durante
    // varios chequeos consecutivos.
    motor_state_t state;
    float reference_position = 0.0f;
    uint8_t suspicious_count = 0;
    bool was_moving = false;
    TickType_t movement_start_tick = 0;
    TickType_t last_check_tick = 0;
    TickType_t cooldown_until_tick = 0;

    (void)pvParameters;

    while (1) {
        if (xQueueReceive(MotorStateQueue, &state, portMAX_DELAY) == pdTRUE) {
            TickType_t now = xTaskGetTickCount();
            float abs_error = fabsf(state.error);

            if (!state.moving || abs_error <= BLOCK_ERROR_MIN) {
                was_moving = false;
                suspicious_count = 0;
                continue;
            }

            if (state.pwm <= BLOCK_PWM_MIN) {
                continue;
            }

            if ((int32_t)(now - cooldown_until_tick) < 0) {
                continue;
            }

            if (!was_moving) {
                // Se da una ventana inicial para que el motor venza la inercia
                // antes de evaluar bloqueo.
                was_moving = true;
                movement_start_tick = now;
                last_check_tick = now;
                reference_position = state.position;
                suspicious_count = 0;
                continue;
            }

            if ((now - movement_start_tick) < pdMS_TO_TICKS(BLOCK_START_GRACE_MS) ||
                (now - last_check_tick) < pdMS_TO_TICKS(BLOCK_CHECK_PERIOD_MS)) {
                continue;
            }

            float delta = fabsf(state.position - reference_position);

            if (delta > 180.0f) {
                delta = 360.0f - delta;
            }

            bool no_position_progress = delta < BLOCK_DELTA_MIN;

            if (no_position_progress) {
                if (suspicious_count < UINT8_MAX) {
                    suspicious_count++;
                }

                ESP_LOGW(TAG,
                         "Safety_Task: sospecha bloqueo, delta=%.2f err=%.2f pwm=%.1f checks=%u/%u",
                         delta,
                         state.error,
                         state.pwm,
                         (unsigned)suspicious_count,
                         (unsigned)BLOCK_CONSECUTIVE_LIMIT);
            } else {
                suspicious_count = 0;
                reference_position = state.position;
            }

            if (suspicious_count >= BLOCK_CONSECUTIVE_LIMIT) {
                // El PID interpreta este comando invirtiendo el sentido de giro.
                control_cmd_t cmd = {
                    .type = CMD_BLOCK_DETECTED,
                    .value = 0.0f
                };

                xQueueSend(ControlQueue, &cmd, 0);

                ESP_LOGW(TAG,
                         "Safety_Task: bloqueo detectado, delta=%.2f err=%.2f pwm=%.1f checks=%u",
                         delta,
                         state.error,
                         state.pwm,
                         (unsigned)suspicious_count);

                suspicious_count = 0;
                was_moving = false;
                cooldown_until_tick = now + pdMS_TO_TICKS(BLOCK_COOLDOWN_MS);
            }

            last_check_tick = now;
        }
    }
}

// ============================================================
// TASK: UART_COMMAND_TASK - PRIORIDAD 2
// ============================================================

static void uart_send_result(int code)
{
    static const char *const responses[] = {
        "OK",
        "ERROR 1 INVALID_COMMAND",
        "ERROR 2 INVALID_VALUE",
        "ERROR 3 DATA_UNAVAILABLE"
    };
    uart_send_line(responses[code]);
}

static void uart_reply_get(uart_field_t field)
{
    char response[UART_PROTOCOL_RESPONSE_SIZE];
    telemetry_t snapshot;
    int length;

    if (field == UART_FIELD_POSITION) {
        float position;
        if (xQueuePeek(angleQueue, &position, 0) != pdTRUE) {
            uart_send_result(3);
            return;
        }
        length = snprintf(response, sizeof(response), "OK POSITION=%.2f", position);
    } else {
        if (xQueuePeek(TelemetryQueue, &snapshot, 0) != pdTRUE) {
            uart_send_result(3);
            return;
        }
        switch (field) {
            case UART_FIELD_SETPOINT:
                length = snprintf(response, sizeof(response), "OK SETPOINT=%.2f", snapshot.config.setpoint);
                break;
            case UART_FIELD_KP:
                length = snprintf(response, sizeof(response), "OK KP=%.3f", snapshot.config.kp);
                break;
            case UART_FIELD_KI:
                length = snprintf(response, sizeof(response), "OK KI=%.3f", snapshot.config.ki);
                break;
            case UART_FIELD_KD:
                length = snprintf(response, sizeof(response), "OK KD=%.3f", snapshot.config.kd);
                break;
            case UART_FIELD_PROFILE:
                length = snprintf(response, sizeof(response), "OK PROFILE=%s",
                                  snapshot.config.profile == PROFILE_ESCALON ? "ESCALON" : "RAMPA");
                break;
            case UART_FIELD_STATUS:
                if (!snapshot.position_valid) {
                    uart_send_result(3);
                    return;
                }
                length = snprintf(response, sizeof(response),
                                  "OK POSITION=%.2f SETPOINT=%.2f ERROR=%.2f PWM=%.1f "
                                  "DIRECTION=%d MOVING=%d KP=%.3f KI=%.3f KD=%.3f PROFILE=%s",
                                  snapshot.motor.position, snapshot.config.setpoint,
                                  snapshot.motor.error, snapshot.motor.pwm,
                                  snapshot.motor.direction, snapshot.motor.moving ? 1 : 0,
                                  snapshot.config.kp, snapshot.config.ki, snapshot.config.kd,
                                  snapshot.config.profile == PROFILE_ESCALON ? "ESCALON" : "RAMPA");
                break;
            default:
                uart_send_result(1);
                return;
        }
    }
    if (length < 0 || (size_t)length >= sizeof(response)) {
        uart_send_result(3);
        return;
    }
    uart_send_line(response);
}

static void uart_apply_set(const uart_request_t *request)
{
    control_cmd_t control_cmd = {
        .value = request->value,
        .completion_task = xTaskGetCurrentTaskHandle()
    };
    display_msg_t display_msg = {.type = DISPLAY_SHOW_MESSAGE};
    config_msg_t config_msg = {.type = CONFIG_SAVE_ALL};
    telemetry_t snapshot;

    switch (request->field) {
        case UART_FIELD_ANGLE:
            control_cmd.type = CMD_SET_ANGLE;
            control_cmd.value = angle_normalize(request->value);
            display_msg.type = DISPLAY_SHOW_SETPOINT;
            snprintf(display_msg.text, sizeof(display_msg.text), "Angle %.1f", control_cmd.value);
            break;
        case UART_FIELD_KP:
            control_cmd.type = CMD_SET_KP;
            snprintf(display_msg.text, sizeof(display_msg.text), "KP %.2f", control_cmd.value);
            break;
        case UART_FIELD_KI:
            control_cmd.type = CMD_SET_KI;
            snprintf(display_msg.text, sizeof(display_msg.text), "KI %.3f", control_cmd.value);
            break;
        case UART_FIELD_KD:
            control_cmd.type = CMD_SET_KD;
            snprintf(display_msg.text, sizeof(display_msg.text), "KD %.2f", control_cmd.value);
            break;
        case UART_FIELD_PROFILE:
            control_cmd.type = CMD_SET_PROFILE;
            display_msg.type = DISPLAY_SHOW_PROFILE;
            snprintf(display_msg.text, sizeof(display_msg.text), "%s",
                     control_cmd.value == PROFILE_ESCALON ? "ESCALON" : "RAMPA");
            break;
        default:
            uart_send_result(1);
            return;
    }

    // El ACK confirma aplicacion en PID, no solo ingreso a la cola.
    xQueueSend(ControlQueue, &control_cmd, portMAX_DELAY);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (xQueuePeek(TelemetryQueue, &snapshot, 0) != pdTRUE) {
        uart_send_result(3);
        return;
    }

    switch (request->field) {
        case UART_FIELD_ANGLE:   g_config.setpoint = snapshot.config.setpoint; break;
        case UART_FIELD_KP:      g_config.kp = snapshot.config.kp; break;
        case UART_FIELD_KI:      g_config.ki = snapshot.config.ki; break;
        case UART_FIELD_KD:      g_config.kd = snapshot.config.kd; break;
        case UART_FIELD_PROFILE: g_config.profile = snapshot.config.profile; break;
        default: break;
    }
    config_msg.config = snapshot.config;
    xQueueSend(ConfigQueue, &config_msg, portMAX_DELAY);
    display_msg.value = control_cmd.value;
    xQueueSend(DisplayQueue, &display_msg, 0);
    uart_send_result(0);
}

static void UART_Command_Task(void *pvParameters)
{
    uart_line_reader_t reader = {0};
    uart_request_t request;
    (void)pvParameters;

    // Storage termina la restauracion antes de admitir cambios remotos.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (1) {
#if UART_USE_CONSOLE
        int ch = getchar();
        if (ch < 0) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (ch == '\b' || ch == 0x7F) {
            if (!reader.rejected && reader.length > 0) {
                reader.length--;
            }
            continue;
        }
#else
        uint8_t ch;
        if (uart_read_bytes(UART_PORT, &ch, 1, portMAX_DELAY) <= 0) {
            continue;
        }
#endif
        int result = uart_protocol_feed(&reader, (unsigned char)ch, &request);
        if (result < 0) {
            continue;
        }
        if (result != 0) {
            uart_send_result(result);
        } else if (request.is_set) {
            uart_apply_set(&request);
        } else {
            uart_reply_get(request.field);
        }
    }
}

// ============================================================
// TASK: BUTTON_HANDLER_TASK - PRIORIDAD 3
// ============================================================

static void ButtonHandler_Task(void *pvParameters)
{
    // Interfaz local de cinco menus:
    // ANGULO se edita con +/- y se ejecuta con OK.
    // Kp/Ki/Kd/PERFIL se aplican y guardan al cambiar con +/-.
    ui_menu_t menu = MENU_ANGLE;
    float selected_angle = g_config.setpoint;
    float selected_kp = g_config.kp;
    float selected_ki = g_config.ki;
    float selected_kd = g_config.kd;
    movement_profile_t selected_profile = g_config.profile;

    display_msg_t display_msg;
    control_cmd_t control_cmd = {0};
    config_msg_t config_msg;
    TickType_t next_plus_repeat_tick = 0;
    TickType_t next_minus_repeat_tick = 0;

    (void)pvParameters;

    display_msg.type = DISPLAY_SHOW_SETPOINT;
    display_msg.value = selected_angle;
    snprintf(display_msg.text, sizeof(display_msg.text), "OK mueve");
    xQueueSend(DisplayQueue, &display_msg, 0);

    while (1) {
        button_event_t button_event;
        BaseType_t event_received = xQueueReceive(
            ButtonEventQueue,
            &button_event,
            pdMS_TO_TICKS(20)
        );
        bool menu_pressed = event_received == pdTRUE && button_event.button == BUTTON_MENU;
        bool ok_pressed = event_received == pdTRUE && button_event.button == BUTTON_OK;
        BaseType_t plus_edge = (event_received == pdTRUE && button_event.button == BUTTON_PLUS) ? pdTRUE : pdFALSE;
        BaseType_t minus_edge = (event_received == pdTRUE && button_event.button == BUTTON_MINUS) ? pdTRUE : pdFALSE;

        if (menu_pressed) {
            // MENU solo cambia la pantalla activa; no mueve el motor.
            menu = (ui_menu_t)((menu + 1) % MENU_COUNT);
            next_plus_repeat_tick = 0;
            next_minus_repeat_tick = 0;

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
            drain_button_queue();
            continue;
        }

        TickType_t now = xTaskGetTickCount();
        bool plus_is_down = gpio_get_level(PIN_BTN_PLUS) == 1;
        bool minus_is_down = gpio_get_level(PIN_BTN_MINUS) == 1;
        BaseType_t plus_pressed = pdFALSE;
        BaseType_t minus_pressed = pdFALSE;

        if (plus_edge == pdTRUE) {
            plus_pressed = pdTRUE;
            next_plus_repeat_tick = now + pdMS_TO_TICKS(BUTTON_HOLD_START_MS);
        } else if (plus_is_down && next_plus_repeat_tick != 0 &&
                   (int32_t)(now - next_plus_repeat_tick) >= 0) {
            plus_pressed = pdTRUE;
            next_plus_repeat_tick = now + pdMS_TO_TICKS(BUTTON_HOLD_REPEAT_MS);
        } else if (!plus_is_down) {
            next_plus_repeat_tick = 0;
        }

        if (minus_edge == pdTRUE) {
            minus_pressed = pdTRUE;
            next_minus_repeat_tick = now + pdMS_TO_TICKS(BUTTON_HOLD_START_MS);
        } else if (minus_is_down && next_minus_repeat_tick != 0 &&
                   (int32_t)(now - next_minus_repeat_tick) >= 0) {
            minus_pressed = pdTRUE;
            next_minus_repeat_tick = now + pdMS_TO_TICKS(BUTTON_HOLD_REPEAT_MS);
        } else if (!minus_is_down) {
            next_minus_repeat_tick = 0;
        }

        if (plus_pressed == pdTRUE || minus_pressed == pdTRUE) {
            int direction = plus_pressed == pdTRUE ? 1 : -1;

            // En ANGULO solo se prepara el nuevo valor. En los otros menus el
            // cambio se aplica inmediatamente al PID y se guarda en NVS.
            switch (menu) {
                case MENU_ANGLE:
                    selected_angle = step_angle_by_5(selected_angle, direction);
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
            drain_button_queue();
            continue;
        }

        if (ok_pressed) {
            next_plus_repeat_tick = 0;
            next_minus_repeat_tick = 0;

            if (menu == MENU_ANGLE) {
                // OK en ANGULO es la unica accion de boton que inicia movimiento.
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
            drain_button_queue();
            continue;
        }

        if (event_received != pdTRUE) {
            continue;
        }
    }
}

// ============================================================
// TASK: DISPLAY_TASK - PRIORIDAD 1
// ============================================================

static void Display_Task(void *pvParameters)
{
    // Display_Task recibe mensajes logicos y los convierte en dos lineas de LCD.
    // La escritura fisica se delega al I2C_Manager_Task para no competir por I2C.
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
    // Unica tarea que escribe/lee NVS. Asi se evita que varias tareas accedan a
    // memoria no volatil al mismo tiempo.
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
        control_cmd_t control_cmd = {0};

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

        control_cmd.type = CMD_SET_ANGLE;
        control_cmd.value = g_config.setpoint;
        xQueueSend(ControlQueue, &control_cmd, 0);

        // Al iniciar, vuelve automaticamente al ultimo setpoint guardado.
        // El ángulo queda recuperado como configuración y se puede mostrar en el display.

        display_msg_t display_msg;
        display_msg.type = DISPLAY_SHOW_MESSAGE;
        display_msg.value = g_config.setpoint;
        snprintf(display_msg.text, sizeof(display_msg.text), "Auto %.1f", g_config.setpoint);
        xQueueSend(DisplayQueue, &display_msg, 0);
    } else {
        ESP_LOGW(TAG, "No había configuración guardada. Se usan valores por defecto.");
    }

    // UART espera a que la restauracion inicial haya enviado todos sus comandos.
    xTaskNotifyGive((TaskHandle_t)pvParameters);

    while (1) {
        if (xQueueReceive(ConfigQueue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.type) {
                case CONFIG_SAVE_ALL:
                    // Un guardado pendiente no debe restaurar valores viejos en RAM.
                    save_config_to_nvs(&msg.config);
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
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE
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
    ESP_ERROR_CHECK(motor_driver_init_pwm());
}

// ============================================================
// INICIALIZACIÓN UART
// ============================================================

static void init_uart(void)
{
#if UART_USE_CONSOLE
    setvbuf(stdin, NULL, _IONBF, 0);
    ESP_LOGI(TAG, "Comandos UART por consola USB");
#else
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
#endif
}

// ============================================================
// APP_MAIN
// ============================================================

void app_main(void)
{
#if UART_USE_CONSOLE
    // En este modo logs y protocolo comparten stdout: reservarlo a respuestas.
    esp_log_level_set("*", ESP_LOG_NONE);
#endif
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    system_config_t saved_config;
    if (load_config_from_nvs(&saved_config)) {
        g_config = saved_config;

        ESP_LOGI(TAG, "Configuracion inicial desde NVS");
        ESP_LOGI(TAG, "Setpoint: %.2f | Kp: %.2f | Ki: %.3f | Kd: %.2f | Profile: %d",
                 g_config.setpoint,
                 g_config.kp,
                 g_config.ki,
                 g_config.kd,
                 g_config.profile);
    } else {
        ESP_LOGW(TAG, "No habia configuracion guardada. Se usan valores por defecto.");
    }

    init_gpio();
    init_i2c();
    init_pwm();
    init_uart();

    // Mapa de comunicacion entre tareas:
    // ControlQueue: UI/UART/Storage/Safety -> PID_Task.
    // angleQueue: AS5600_Reader_Task -> PID_Task.
    // MotorStateQueue: PID_Task -> Safety_Task.
    // TelemetryQueue: PID_Task -> UART (peek, independiente de Safety).
    // DisplayQueue: UI/UART/PID/Storage -> Display_Task.
    // ConfigQueue: UI/UART -> Storage_Task.
    // ButtonEventQueue: ISR botones -> ButtonHandler_Task.
    // I2C_TXQueue/I2C_RXQueue: AS5600/LCD tasks -> I2C_Manager_Task.
    ControlQueue = xQueueCreate(10, sizeof(control_cmd_t));
    I2C_TXQueue = xQueueCreate(10, sizeof(i2c_request_t));
    I2C_RXQueue = xQueueCreate(10, sizeof(i2c_response_t));

    angleQueue = xQueueCreate(1, sizeof(float));
    MotorStateQueue = xQueueCreate(1, sizeof(motor_state_t));
    TelemetryQueue = xQueueCreate(1, sizeof(telemetry_t));

    DisplayQueue = xQueueCreate(10, sizeof(display_msg_t));
    ConfigQueue = xQueueCreate(5, sizeof(config_msg_t));
    ButtonEventQueue = xQueueCreate(10, sizeof(button_event_t));

    if (!ControlQueue ||
        !I2C_TXQueue ||
        !I2C_RXQueue ||
        !angleQueue ||
        !MotorStateQueue ||
        !TelemetryQueue ||
        !DisplayQueue ||
        !ConfigQueue ||
        !ButtonEventQueue) {

        ESP_LOGE(TAG, "Error creando colas");
        return;
    }

    // Prioridad 3
    xTaskCreate(PID_Task, "PID_Task", 4096, NULL, 3, NULL);
    xTaskCreate(I2C_Manager_Task, "I2C_Manager_Task", 4096, NULL, 3, NULL);
    xTaskCreate(AS5600_Reader_Task, "AS5600_Reader_Task", 4096, NULL, 3, NULL);
    xTaskCreate(ButtonHandler_Task, "ButtonHandler_Task", 4096, NULL, 3, NULL);

    // Prioridad 2
    xTaskCreate(Safety_Task, "Safety_Task", 4096, NULL, 2, NULL);
    TaskHandle_t uart_task = NULL;
    if (xTaskCreate(UART_Command_Task, "UART_Command_Task", 4096, NULL, 2, &uart_task) != pdPASS) {
        ESP_LOGE(TAG, "Error creando UART_Command_Task");
        return;
    }


    // Prioridad 1
    xTaskCreate(Display_Task, "Display_Task", 4096, NULL, 1, NULL);
    xTaskCreate(Storage_Task, "Storage_Task", 4096, uart_task, 1, NULL);

    ESP_LOGI(TAG, "Sistema iniciado");
}
