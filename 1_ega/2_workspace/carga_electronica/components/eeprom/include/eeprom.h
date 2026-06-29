#ifndef EEPROM_MANAGER_H
#define EEPROM_MANAGER_H

#include "system_manager.h" 
#include <stdbool.h>

void eeprom_init(void);
void eeprom_save_config(system_mode_t mode, float setpoint_cc, float setpoint_cr);
bool eeprom_load_config(system_mode_t *mode, float *setpoint_cc, float *setpoint_cr);

#endif