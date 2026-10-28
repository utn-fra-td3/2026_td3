#ifndef SETTINGS_STORAGE_H
#define SETTINGS_STORAGE_H

#include <stdbool.h>

typedef struct {
    float voltage_positive;
    float voltage_negative;
    float current_positive;
    float current_negative;
    float overvoltage_positive;
    float overvoltage_negative;
} stored_settings_t;

bool settings_storage_load(stored_settings_t *settings);
void settings_storage_save(const stored_settings_t *settings);

#endif
