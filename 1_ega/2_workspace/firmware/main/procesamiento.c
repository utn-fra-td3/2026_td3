#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include "tipos_sistema.h"
#include "guardiana.h"
#include "procesamiento.h"

void vTareaProcesamiento(void *pvParameters) {
    colas_sistema_t *colas = (colas_sistema_t *) pvParameters;
    datos_crudos_t datos;

    while(1) {
        // Bloquea indefinidamente hasta recibir lecturas físicas del ADC
        if (xQueueReceive(colas->q_datos_crudos, &datos, portMAX_DELAY) == pdTRUE) {
            
            // Cálculo: Punto flotante y logaritmos aislados del hardware
            float ganancia_lineal = datos.vout_crudo / datos.vin_crudo;
            float ganancia_db = 20.0 * log10(ganancia_lineal);

            //Imprime en consola el resultado del cálculo de ganancia en dB para la frecuencia actual
            imprimir_consola("{\"tipo\":\"dato\", \"f\":%d, \"g\":%.2f}\n", datos.frecuencia_hz, ganancia_db);
        }
    }
}