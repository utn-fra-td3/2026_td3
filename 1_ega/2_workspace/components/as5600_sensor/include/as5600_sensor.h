#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t as5600_read_raw(uint16_t *raw);
