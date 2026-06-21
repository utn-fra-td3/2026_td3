#ifndef UI_SCRCONFIG_KB_H
#define UI_SCRCONFIG_KB_H

// --- Includes ---
#include <stdint.h>

// --- Prototipos publicos ---
void ui_scrconfig_kb_init(void); // llamar una vez creada ui_scrconfig (despues de ui_init())

uint32_t ui_config_get_frec_inicio(void);
uint32_t ui_config_get_frec_final(void);
uint32_t ui_config_get_puntos(void);
uint32_t ui_config_get_tiempo(void);

#endif // UI_SCRCONFIG_KB_H
