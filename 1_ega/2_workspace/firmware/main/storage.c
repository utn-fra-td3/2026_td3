#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "guardiana.h"

//Para guardar ultima configuración de barrido en la memoria flash del ESP32 (NVS)

void guardar_configuracion(int f_ini, int f_fin, int paso_idx, int tiempo, bool set_ini, bool set_fin) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_i32(my_handle, "f_ini", f_ini);
        nvs_set_i32(my_handle, "f_fin", f_fin);
        nvs_set_i32(my_handle, "paso_idx", paso_idx);
        nvs_set_i32(my_handle, "tiempo", tiempo);
        nvs_set_u8(my_handle, "set_ini", set_ini ? 1 : 0);
        nvs_set_u8(my_handle, "set_fin", set_fin ? 1 : 0);
        nvs_commit(my_handle); 
        nvs_close(my_handle);
        imprimir_consola(">>> Configuración AUTOGUARDADA en Flash <<<\n");
    }
}

//Para cargar la configuración previa desde la memoria flash del ESP32 (NVS)

void cargar_configuracion(int *f_ini, int *f_fin, int *paso_idx, int *tiempo, bool *set_ini, bool *set_fin) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READONLY, &my_handle) == ESP_OK) {
        int32_t val_i32; 
        uint8_t val_u8;
        if (nvs_get_i32(my_handle, "f_ini", &val_i32) == ESP_OK) *f_ini = val_i32;
        if (nvs_get_i32(my_handle, "f_fin", &val_i32) == ESP_OK) *f_fin = val_i32;
        if (nvs_get_i32(my_handle, "paso_idx", &val_i32) == ESP_OK) *paso_idx = val_i32;
        if (nvs_get_i32(my_handle, "tiempo", &val_i32) == ESP_OK) *tiempo = val_i32;
        if (nvs_get_u8(my_handle, "set_ini", &val_u8) == ESP_OK) *set_ini = (val_u8 > 0);
        if (nvs_get_u8(my_handle, "set_fin", &val_u8) == ESP_OK) *set_fin = (val_u8 > 0);
        nvs_close(my_handle);
        imprimir_consola(">>> Configuración CARGADA desde Flash exitosamente <<<\n");
    } else {
        imprimir_consola("No hay configuración previa. Iniciando valores por defecto.\n");
    }
}
