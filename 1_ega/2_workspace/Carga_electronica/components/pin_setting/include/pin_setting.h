#ifndef PIN_SETTING_H
#define PIN_SETTING_H

#include "hal/adc_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================
// DEFINICIÓN GLOBAL DE PINES DEL HARDWARE
// ==========================================

// --- ENCODER ---
#define ENCODER_PIN_A       4   
#define ENCODER_PIN_B       5
#define ENCODER_PIN_SW      6   

// --- ALARMAS LEDS ---
#define ALARM_OCP_PIN       15
#define ALARM_OVP_PIN       16

// --- DISPLAY ---
// Nota: La configuración SPI del Display está ahora en communication_setting.h

// --- I2C ---
// Nota: La configuración I2C está ahora en communication_setting.h

// --- ADC SENSOR ---
#define ADC_UNIT            ADC_UNIT_1
#define ADC_VOLTAGE_CHAN    ADC_CHANNEL_0 // GPIO 1
#define ADC_CURRENT_CHAN    ADC_CHANNEL_1 // GPIO 2

/**
 * @brief Inicialización dummy para cumplir con el requerimiento de componente en C.
 */
void pin_setting_init(void);

#ifdef __cplusplus
}
#endif

#endif // PIN_SETTING_H
