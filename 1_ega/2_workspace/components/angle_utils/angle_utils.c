#include "angle_utils.h"

#include "app_config.h"

float angle_normalize(float angle)
{
    // Mantiene cualquier angulo dentro del rango [0, 360).
    while (angle >= 360.0f) {
        angle -= 360.0f;
    }

    while (angle < 0.0f) {
        angle += 360.0f;
    }

    return angle;
}

float angle_shortest_error(float setpoint, float position)
{
    // Error firmado por el camino mas corto. El resultado queda en
    // [-180, 180], lo que evita giros innecesarios.
    float error = setpoint - position;

    while (error > 180.0f) {
        error -= 360.0f;
    }

    while (error < -180.0f) {
        error += 360.0f;
    }

    return error;
}

float angle_raw_to_degrees(uint16_t raw)
{
    // El AS5600 usa 4096 cuentas por vuelta.
    float angle = ((float)raw * 360.0f) / 4096.0f;

#if ANGLE_CCW_POSITIVE
    // Ajuste de convencion mecanica: en este montaje los angulos deben crecer
    // en sentido antihorario.
    angle = 360.0f - angle;
#endif

    return angle_normalize(angle);
}
