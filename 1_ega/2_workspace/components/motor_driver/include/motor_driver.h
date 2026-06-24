#pragma once

#include "esp_err.h"

esp_err_t motor_driver_init_pwm(void);
void motor_stop(void);
void motor_brake(void);
void motor_apply(float control_signal);
