#ifndef FLASH_H
#define FLASH_H

#include "config.h"

void flash_init(void);
void flash_task_start(void);
void task_flash(void *param);

int flash_save_config(const config_t *cfg);
int flash_load_config(config_t *cfg);

#endif