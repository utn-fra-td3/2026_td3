#ifndef DIGIPOT_H
#define DIGIPOT_H

#include <stdint.h>

void digipot_init(void);
void task_digipot(void *param);

void digipot_set_cursor(uint8_t value);

#endif