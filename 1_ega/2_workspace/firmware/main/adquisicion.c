#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "tipos_sistema.h"
#include "encoder.h"
#include "guardiana.h"
#include "ad9833.h"
#include "adquisicion.h"

#define ADC_CANAL_VIN  ADC_CHANNEL_1 // GPIO2
#define ADC_CANAL_VOUT ADC_CHANNEL_0 // GPIO1

void vTareaAdquisicion(void *pvParameters) {
    colas_sistema_t *colas = (colas_sistema_t *) pvParameters;
    param_barrido_t params;

    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&init_config1, &adc1_handle);

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12, 
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CANAL_VIN, &config);
    adc_oneshot_config_channel(adc1_handle, ADC_CANAL_VOUT, &config);

    while(1) {
        if (xQueueReceive(colas->q_comandos_barrido, &params, portMAX_DELAY) == pdTRUE) {
            
            for (int f = params.freq_inicial; f <= params.freq_final; f += params.paso) {
                ad9833_set_frequency(f);
                vTaskDelay(pdMS_TO_TICKS(params.tiempo_ms));

                uint32_t suma_vin = 0;
                uint32_t suma_vout = 0;
                
                int num_muestras = (f < 100) ? 100 : 20;
                int delay_muestreo = (f < 10) ? 10 : 1; 

                for (int i = 0; i < num_muestras; i++) {
                    int val_vin = 0;
                    int val_vout = 0;
                    adc_oneshot_read(adc1_handle, ADC_CANAL_VIN, &val_vin);
                    adc_oneshot_read(adc1_handle, ADC_CANAL_VOUT, &val_vout);
                    
                    suma_vin += val_vin;
                    suma_vout += val_vout;
                    vTaskDelay(pdMS_TO_TICKS(delay_muestreo)); 
                }

                float vin_promedio = (float)suma_vin / num_muestras;
                float vout_promedio = (float)suma_vout / num_muestras;
                if (vin_promedio < 1.0f) vin_promedio = 1.0f;

                datos_crudos_t crudos = {f, vin_crudo: vin_promedio, vout_crudo: vout_promedio};
                xQueueSend(colas->q_datos_crudos, &crudos, portMAX_DELAY);
            }
            
            imprimir_consola("{\"tipo\":\"info\", \"msg\":\"FIN_SECUENCIA\"}\n");
            encoder_event_t evento_fin = EVENTO_BARRIDO_FIN;
            xQueueSend(colas->q_encoder, &evento_fin, 0);
        }
    }
}