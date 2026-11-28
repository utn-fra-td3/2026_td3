#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>

void guardar_configuracion(int f_ini, int f_fin, int paso_idx, int tiempo, bool set_ini, bool set_fin);
void cargar_configuracion(int *f_ini, int *f_fin, int *paso_idx, int *tiempo, bool *set_ini, bool *set_fin);

#endif // STORAGE_H
