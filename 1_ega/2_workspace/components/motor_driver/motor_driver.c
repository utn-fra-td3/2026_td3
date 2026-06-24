#include "motor_driver.h"

#include <math.h>
#include <stdint.h>

#include "app_config.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

esp_err_t motor_driver_init_pwm(void)
{
    // LEDC genera el PWM que entra al pin EN del L298N.
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE_USED,
        .timer_num = LEDC_TIMER_USED,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };

    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        return ret;
    }

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE_USED,
        .channel = LEDC_CHANNEL_USED,
        .timer_sel = LEDC_TIMER_USED,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = PIN_MOTOR_PWM,
        .duty = 0,
        .hpoint = 0
    };

    return ledc_channel_config(&ledc_channel);
}

void motor_stop(void)
{
    // Parada libre: PWM en cero e IN1/IN2 bajos.
    ledc_set_duty(LEDC_MODE_USED, LEDC_CHANNEL_USED, 0);
    ledc_update_duty(LEDC_MODE_USED, LEDC_CHANNEL_USED);

    gpio_set_level(PIN_MOTOR_IN1, 0);
    gpio_set_level(PIN_MOTOR_IN2, 0);
}

void motor_brake(void)
{
    // Freno electrico del puente H: IN1 e IN2 altos con duty maximo.
    gpio_set_level(PIN_MOTOR_IN1, 1);
    gpio_set_level(PIN_MOTOR_IN2, 1);

    ledc_set_duty(LEDC_MODE_USED, LEDC_CHANNEL_USED, LEDC_MAX_DUTY);
    ledc_update_duty(LEDC_MODE_USED, LEDC_CHANNEL_USED);
}

void motor_apply(float control_signal)
{
    float pwm = fabsf(control_signal);
    float motor_signal = control_signal;

#if ANGLE_CCW_POSITIVE
    // La mecanica quedo invertida respecto del sensor, por eso el comando al
    // motor se invierte para que los angulos positivos sean antihorarios.
    motor_signal = -motor_signal;
#endif

    if (pwm > PWM_MAX) {
        pwm = PWM_MAX;
    }

    if (pwm > 0.0f && pwm < PWM_MIN) {
        // Por debajo de este PWM el motor puede no vencer el rozamiento.
        pwm = PWM_MIN;
    }

    if (motor_signal > 0.0f) {
        gpio_set_level(PIN_MOTOR_IN1, 1);
        gpio_set_level(PIN_MOTOR_IN2, 0);
    } else if (motor_signal < 0.0f) {
        gpio_set_level(PIN_MOTOR_IN1, 0);
        gpio_set_level(PIN_MOTOR_IN2, 1);
    } else {
        motor_stop();
        return;
    }

    ledc_set_duty(LEDC_MODE_USED, LEDC_CHANNEL_USED, (uint32_t)pwm);
    ledc_update_duty(LEDC_MODE_USED, LEDC_CHANNEL_USED);
}
