#ifndef VOUT_NEG_CALIBRATION_H
#define VOUT_NEG_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>

#ifndef ADS1115_FSR_VOLTS
#define ADS1115_FSR_VOLTS 4.096f
#endif

#define VOUT2_PIN_ZERO_OFFSET_VOLTS 0.000f
#define VOUT2_INPUT_R_OHMS 820000.0f
#define VOUT2_FEEDBACK_R_OHMS 51000.0f
#define VOUT2_OFFSET 0.00000f

typedef struct {
    float measured_volts;
    float real_volts;
} vout_neg_calibration_point_t;

/*
 * Calibracion final de Vout negativa, expresada como magnitud respecto de GND.
 * measured_volts: tension obtenida despues de convertir la lectura del ADC.
 * real_volts: magnitud real en la salida, incluida la caida en Rshunt.
 *
 * Esta unica tabla combina la calibracion anterior con las mediciones de
 * verificacion del 14/09/2026. No se aplica un segundo offset ni otra tabla.
 * Los puntos entre 0 V y 30 V estan separados exactamente 0,5 V.
 */
static const vout_neg_calibration_point_t VOUT_NEG_CALIBRATION_POINTS[] = {
/*   ADC convertido    Salida real  */
    {       0.0f,          0.000f },
    {       0.5f,          0.511f },
    {       1.0f,          1.029f },
    {       1.5f,          1.547f },
    {       2.0f,          2.065f },
    {       2.5f,          2.583f },
    {       3.0f,          3.113f },
    {       3.5f,          3.625f },
    {       4.0f,          4.136f },
    {       4.5f,          4.648f },
    {       5.0f,          5.160f },
    {       5.5f,          5.675f },
    {       6.0f,          6.189f },
    {       6.5f,          6.702f },
    {       7.0f,          7.216f },
    {       7.5f,          7.730f },
    {       8.0f,          8.244f },
    {       8.5f,          8.758f },
    {       9.0f,          9.272f },
    {       9.5f,          9.786f },
    {      10.0f,         10.299f },
    {      10.5f,         10.815f },
    {      11.0f,         11.330f },
    {      11.5f,         11.846f },
    {      12.0f,         12.362f },
    {      12.5f,         12.885f },
    {      13.0f,         13.399f },
    {      13.5f,         13.913f },
    {      14.0f,         14.427f },
    {      14.5f,         14.941f },
    {      15.0f,         15.448f },
    {      15.5f,         15.964f },
    {      16.0f,         16.480f },
    {      16.5f,         16.996f },
    {      17.0f,         17.512f },
    {      17.5f,         18.026f },
    {      18.0f,         18.542f },
    {      18.5f,         19.058f },
    {      19.0f,         19.573f },
    {      19.5f,         20.089f },
    {      20.0f,         20.607f },
    {      20.5f,         21.111f },
    {      21.0f,         21.615f },
    {      21.5f,         22.118f },
    {      22.0f,         22.622f },
    {      22.5f,         23.101f },
    {      23.0f,         23.589f },
    {      23.5f,         24.078f },
    {      24.0f,         24.566f },
    {      24.5f,         25.062f },
    {      25.0f,         25.632f },
    {      25.5f,         26.172f },
    {      26.0f,         26.711f },
    {      26.5f,         27.251f },
    {      27.0f,         27.785f },
    {      27.5f,         28.271f },
    {      28.0f,         28.787f },
    {      28.5f,         29.303f },
    {      29.0f,         29.818f },
    {      29.5f,         30.334f },
    {      30.0f,         30.849f }
};

static inline float vout_neg_calibrate(float measured_volts) {
    const size_t count = sizeof(VOUT_NEG_CALIBRATION_POINTS) / sizeof(VOUT_NEG_CALIBRATION_POINTS[0]);
    if (measured_volts <= VOUT_NEG_CALIBRATION_POINTS[0].measured_volts) return VOUT_NEG_CALIBRATION_POINTS[0].real_volts;
    size_t upper = 1U;
    while (upper < count && measured_volts > VOUT_NEG_CALIBRATION_POINTS[upper].measured_volts) upper++;
    if (upper == count) upper = count - 1U;
    const vout_neg_calibration_point_t lower_point = VOUT_NEG_CALIBRATION_POINTS[upper - 1U];
    const vout_neg_calibration_point_t upper_point = VOUT_NEG_CALIBRATION_POINTS[upper];
    const float fraction = (measured_volts - lower_point.measured_volts) / (upper_point.measured_volts - lower_point.measured_volts);
    const float calibrated = lower_point.real_volts + fraction * (upper_point.real_volts - lower_point.real_volts);
    if (calibrated < 0.0f) {
        return 0.0f;
    }
    return calibrated;
}

static inline float vout_neg_from_ads_raw(uint16_t raw) {
    float pin_voltage = (float)raw * ADS1115_FSR_VOLTS / 32768.0f - VOUT2_PIN_ZERO_OFFSET_VOLTS;
    if (pin_voltage < 0.0f) {
        pin_voltage = 0.0f;
    }
    if (pin_voltage > ADS1115_FSR_VOLTS) {
        pin_voltage = ADS1115_FSR_VOLTS;
    }
    float measured_voltage = pin_voltage * VOUT2_INPUT_R_OHMS / VOUT2_FEEDBACK_R_OHMS + VOUT2_OFFSET;
    return vout_neg_calibrate(measured_voltage);
}

#endif
