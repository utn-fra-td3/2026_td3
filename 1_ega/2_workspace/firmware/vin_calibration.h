#ifndef VIN_CALIBRATION_H
#define VIN_CALIBRATION_H

#include <stddef.h>
#include <stdint.h>

#define ADC_REFERENCE_VOLTAGE 3.300f
#define ADC_MAX_VALUE 4095.0f
#define VIN_R1_OHMS 10000.0f
#define VIN_R2_OHMS 2200.0f
#define VIN_OFFSET 0.00000f

typedef struct {
    float source_volts;
    float display_volts;
    float oscilloscope_volts;
} vin_calibration_point_t;

/*
 * Mediciones del 14/09/2026.
 *
 *     Fuente     Display     Osciloscopio
 *
 * La calibracion recibe el valor que mostraba el display sin corregir y
 * devuelve la tension real medida con el osciloscopio.
 */
static const vin_calibration_point_t VIN_CALIBRATION_POINTS[] = {
    { 3.50f,       3.53f,          3.52f },
    { 4.00f,       4.04f,          4.02f },
    { 4.50f,       4.54f,          4.52f },
    { 5.00f,       5.04f,          5.02f },
    { 5.50f,       5.55f,          5.51f },
    { 6.00f,       6.07f,          6.02f },
    { 6.50f,       6.54f,          6.52f },
    { 7.00f,       7.04f,          7.01f },
    { 7.50f,       7.52f,          7.51f },
    { 8.00f,       7.99f,          8.02f },
    { 8.50f,       8.45f,          8.52f },
    { 9.00f,       8.89f,          9.03f },
    { 9.50f,       9.32f,          9.53f },
    {10.00f,       9.72f,         10.04f },
    {10.50f,      10.10f,         10.54f },
    {11.00f,      10.46f,         11.05f },
    {11.50f,      10.79f,         11.53f },
    {12.00f,      11.10f,         12.01f },
    {12.50f,      11.39f,         12.52f },
    {13.00f,      11.66f,         13.03f },
    {13.50f,      11.92f,         13.54f },
    {14.00f,      12.15f,         14.04f },
    {14.50f,      12.38f,         14.55f },
};

static inline float vin_calibrate(float measured_volts) {
    const size_t point_count = sizeof(VIN_CALIBRATION_POINTS) / sizeof(VIN_CALIBRATION_POINTS[0]);

    if (measured_volts < VIN_CALIBRATION_POINTS[0].display_volts) { return measured_volts; }

    for (size_t i = 1U; i < point_count; ++i) {
        const vin_calibration_point_t lower = VIN_CALIBRATION_POINTS[i - 1U];
        const vin_calibration_point_t upper = VIN_CALIBRATION_POINTS[i];

        if (measured_volts <= upper.display_volts) {
            const float fraction = (measured_volts - lower.display_volts) / (upper.display_volts - lower.display_volts);
            return lower.oscilloscope_volts + fraction * (upper.oscilloscope_volts - lower.oscilloscope_volts);
        }
    }

    const vin_calibration_point_t lower = VIN_CALIBRATION_POINTS[point_count - 2U];
    const vin_calibration_point_t upper = VIN_CALIBRATION_POINTS[point_count - 1U];
    const float fraction = (measured_volts - lower.display_volts) / (upper.display_volts - lower.display_volts);
    return lower.oscilloscope_volts + fraction * (upper.oscilloscope_volts - lower.oscilloscope_volts);
}

static inline float vin_from_adc_raw(uint16_t raw) {
    float adc_voltage = (float)raw * ADC_REFERENCE_VOLTAGE / ADC_MAX_VALUE;
    float measured_voltage = adc_voltage * (VIN_R1_OHMS + VIN_R2_OHMS) / VIN_R2_OHMS + VIN_OFFSET;
    if (measured_voltage < 0.0f) {
        measured_voltage = 0.0f;
    }
    return vin_calibrate(measured_voltage);
}

#endif
