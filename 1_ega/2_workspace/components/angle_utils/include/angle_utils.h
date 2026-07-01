#pragma once

#include <stdint.h>

float angle_normalize(float angle);
float angle_shortest_error(float setpoint, float position);
float angle_raw_to_degrees(uint16_t raw);
