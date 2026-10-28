#ifndef VOUT_POS_CALIBRATION_H
#define VOUT_POS_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>

#ifndef ADS1115_FSR_VOLTS
#define ADS1115_FSR_VOLTS 4.096f
#endif

#define VOUT1_PIN_ZERO_OFFSET_VOLTS 0.000f
#define VOUT_R1_OHMS 330000.0f
#define VOUT_R2_OHMS 22000.0f
#define VOUT_OFFSET 0.00000f

typedef struct {
    float measured_volts;
    float real_volts;
} vout_pos_calibration_point_t;

/*
 * Calibracion final de Vout positiva, medida respecto de GND.
 * measured_volts: tension obtenida despues de convertir la lectura del ADC.
 * real_volts: tension real en la salida, incluida la caida en Rshunt.
 *
 * Esta unica tabla combina la calibracion anterior con las mediciones de
 * verificacion del 14/09/2026. No se aplica un segundo offset ni otra tabla.
 * Los puntos entre 0 V y 30 V estan separados exactamente 0,5 V.
 */
static const vout_pos_calibration_point_t VOUT_POS_CALIBRATION_POINTS[] = {
/*   ADC convertido    Salida real  */
    {       0.0f,          0.000f },
    {       0.5f,          0.453f },
    {       1.0f,          0.922f },
    {       1.5f,          1.392f },
    {       2.0f,          1.861f },
    {       2.5f,          2.350f },
    {       3.0f,          2.812f },
    {       3.5f,          3.274f },
    {       4.0f,          3.736f },
    {       4.5f,          4.198f },
    {       5.0f,          4.660f },
    {       5.5f,          5.120f },
    {       6.0f,          5.580f },
    {       6.5f,          6.041f },
    {       7.0f,          6.501f },
    {       7.5f,          6.961f },
    {       8.0f,          7.455f },
    {       8.5f,          7.960f },
    {       9.0f,          8.465f },
    {       9.5f,          8.970f },
    {      10.0f,          9.475f },
    {      10.5f,          9.945f },
    {      11.0f,         10.376f },
    {      11.5f,         10.806f },
    {      12.0f,         11.237f },
    {      12.5f,         11.668f },
    {      13.0f,         12.094f },
    {      13.5f,         12.553f },
    {      14.0f,         13.011f },
    {      14.5f,         13.469f },
    {      15.0f,         13.928f },
    {      15.5f,         14.406f },
    {      16.0f,         14.883f },
    {      16.5f,         15.359f },
    {      17.0f,         15.835f },
    {      17.5f,         16.312f },
    {      18.0f,         16.743f },
    {      18.5f,         17.164f },
    {      19.0f,         17.567f },
    {      19.5f,         17.970f },
    {      20.0f,         18.373f },
    {      20.5f,         18.828f },
    {      21.0f,         19.282f },
    {      21.5f,         19.784f },
    {      22.0f,         20.292f },
    {      22.5f,         20.800f },
    {      23.0f,         21.271f },
    {      23.5f,         21.742f },
    {      24.0f,         22.216f },
    {      24.5f,         22.692f },
    {      25.0f,         23.169f },
    {      25.5f,         23.660f },
    {      26.0f,         24.151f },
    {      26.5f,         24.620f },
    {      27.0f,         25.035f },
    {      27.5f,         25.450f },
    {      28.0f,         25.952f },
    {      28.5f,         26.454f },
    {      29.0f,         26.920f },
    {      29.5f,         27.354f },
    {      30.0f,         27.787f }
};

static inline float vout_pos_calibrate(float measured_volts) {
    const size_t count = sizeof(VOUT_POS_CALIBRATION_POINTS) / sizeof(VOUT_POS_CALIBRATION_POINTS[0]);
    if (measured_volts <= VOUT_POS_CALIBRATION_POINTS[0].measured_volts) return VOUT_POS_CALIBRATION_POINTS[0].real_volts;
    size_t upper = 1U;
    while (upper < count && measured_volts > VOUT_POS_CALIBRATION_POINTS[upper].measured_volts) upper++;
    if (upper == count) upper = count - 1U;
    const vout_pos_calibration_point_t lower_point = VOUT_POS_CALIBRATION_POINTS[upper - 1U];
    const vout_pos_calibration_point_t upper_point = VOUT_POS_CALIBRATION_POINTS[upper];
    const float fraction = (measured_volts - lower_point.measured_volts) / (upper_point.measured_volts - lower_point.measured_volts);
    const float calibrated = lower_point.real_volts + fraction * (upper_point.real_volts - lower_point.real_volts);
    if (calibrated < 0.0f) {
        return 0.0f;
    }
    return calibrated;
}

static inline float vout_pos_from_ads_raw(uint16_t raw) {
    float pin_voltage = (float)raw * ADS1115_FSR_VOLTS / 32768.0f - VOUT1_PIN_ZERO_OFFSET_VOLTS;
    if (pin_voltage < 0.0f) {
        pin_voltage = 0.0f;
    }
    if (pin_voltage > ADS1115_FSR_VOLTS) {
        pin_voltage = ADS1115_FSR_VOLTS;
    }
    float measured_voltage = pin_voltage * (VOUT_R1_OHMS + VOUT_R2_OHMS) / VOUT_R2_OHMS + VOUT_OFFSET;
    return vout_pos_calibrate(measured_voltage);
}

#endif
