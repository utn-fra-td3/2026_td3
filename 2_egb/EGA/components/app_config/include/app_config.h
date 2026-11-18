#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/uart.h"

// L298N
#define PIN_MOTOR_PWM       GPIO_NUM_4
#define PIN_MOTOR_IN1       GPIO_NUM_5
#define PIN_MOTOR_IN2       GPIO_NUM_6

// AS5600
#define PIN_AS5600_DIR      GPIO_NUM_7

// I2C compartido: AS5600 + LCD
#define PIN_I2C_SDA         GPIO_NUM_8
#define PIN_I2C_SCL         GPIO_NUM_7

// Botones
#define PIN_BTN_PLUS        GPIO_NUM_10
#define PIN_BTN_MINUS       GPIO_NUM_11
#define PIN_BTN_MENU        GPIO_NUM_12
#define PIN_BTN_OK          GPIO_NUM_13
#define BUTTON_DEBOUNCE_MS  180
#define BUTTON_HOLD_START_MS 400
#define BUTTON_HOLD_REPEAT_MS 120

// Convencion mecanica: los angulos deben aumentar en sentido antihorario.
#define ANGLE_CCW_POSITIVE  1
// Calibracion mecanica de la aguja: valor que lee el sensor cuando la aguja
// esta fisicamente en 0 grados. Ejemplo: si en cero lee 274.2, poner 274.2f.
#define ANGLE_ZERO_OFFSET_DEG 200.0f

// UART comandos PC
// UART_USE_CONSOLE=1 permite mandar comandos desde el mismo monitor USB usado
// para flashear. Si se pone en 0, se usa UART_PORT por los pines TX/RX.
#define UART_USE_CONSOLE    0
#define PIN_UART_TX         GPIO_NUM_17
#define PIN_UART_RX         GPIO_NUM_18

// I2C
#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         100000

#define AS5600_ADDR         0x36
#define AS5600_REG_ANGLE_H  0x0E
#define AS5600_LOG_ENABLE   0

#define LCD_I2C_ADDR        0x27
#define LCD_I2C_TIMEOUT_MS  50
#define LCD_COLS            16
#define LCD_ROWS            2

// PWM LEDC
#define LEDC_MODE_USED      LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_USED     LEDC_TIMER_0
#define LEDC_CHANNEL_USED   LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT
#define LEDC_MAX_DUTY       1023
#define LEDC_FREQ_HZ        20000

// UART
#define UART_PORT           UART_NUM_1
#define UART_BUF_SIZE       256

// Control
// Tolerancia final de posicion. Debe ser mayor que la zona muerta mecanica:
// si el motor queda clavado a ~1 grado, pedir menos lo deja corrigiendo en loop.
#define PID_PERIOD_MS       10
#define AS5600_PERIOD_MS    10
#define RAMP_SPEED_DEG_S    30.0f

#define ANGLE_ACCEPT_DEG    2.0f

// El motor necesita un PWM alto para empezar a moverse. Cerca del objetivo se
// usan pulsos mas suaves para no pasarse por inercia. Si un bloqueo obligo a
// tomar el camino largo, al acercarse al setpoint se vuelve al camino corto.
#define PWM_MAX             1023.0f
#define PWM_MIN             150.0f
#define PWM_MOVE_MIN        750.0f
#define PWM_FINE_MIN        700.0f
#define PWM_START_BOOST     900.0f
#define PWM_START_BOOST_MS  300
#define TARGET_VERIFY_MS    60
#define FINE_CONTROL_ZONE_DEG 2.0f
#define FORCED_DIRECTION_RELEASE_DEG 10.0f
#define FINE_PULSE_MS       20
#define FINE_BRAKE_MS       120

// Criterios de bloqueo: exige PWM sostenido, error grande y poco cambio de
// posicion durante varios chequeos consecutivos.
#define BLOCK_PWM_MIN       250.0f
#define BLOCK_ERROR_MIN     8.0f
#define BLOCK_DELTA_MIN     5.0f
#define BLOCK_CONSECUTIVE_LIMIT 2
#define BLOCK_COOLDOWN_MS   500
#define BLOCK_START_GRACE_MS 500
#define BLOCK_CHECK_PERIOD_MS 250
