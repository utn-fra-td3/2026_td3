#include "helper.h"

/**
 * @brief Inicializa un PWM en el pin solicitado
 * @param gpio numero de GPIO
 * @param freq frecuencia deseada en Hz
 */
void pwm_user_init(uint32_t gpio, uint32_t freq) {
    // Asigna función de PWM
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    // Configura frecuencia de PWM e inicializa
    uint32_t slice = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv(slice, frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000.0);
    pwm_set_wrap(slice, 1000000 / freq);
    pwm_set_gpio_level(gpio, 500000 / freq);
    pwm_set_enabled(slice, true);
}