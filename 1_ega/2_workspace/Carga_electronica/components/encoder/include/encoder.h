#ifndef ENCODER_PCNT_H
#define ENCODER_PCNT_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Registra una función de callback para cuando se mantenga el botón pulsado (click largo).
 */
void encoder_set_long_press_cb(void (*cb)(void));

/**
 * @brief Inicializa el hardware del encoder (PCNT + GPIO para el botón) 
 * y lo registra como un dispositivo de entrada en LVGL.
 */
void encoder_init(void);

#ifdef __cplusplus
}
#endif

#endif // ENCODER_PCNT_H