#include <stdint.h>
#include "adc.h"
#include "esp_err.h"
#include "esp_adc/adc_continuous.h"
#include "soc/soc_caps.h"


#define ADC_UNIT              ADC_UNIT_1        // ADC1 
#define ADC_CHANNEL_SIGNAL    ADC_CHANNEL_0
#define ADC_CHANNEL_TRIGGER   ADC_CHANNEL_1

#define ADC_ATTEN             ADC_ATTEN_DB_12           // Atenuación de 12 dB para mayor rango de entrada
#define ADC_BITWIDTH          SOC_ADC_DIGI_MAX_BITWIDTH // Ancho de bits máximo para la resolución del ADC
 
#define ADC_POOL_SIZE         32768                     // Tamaño del buffer de almacenamiento interno del ADC continuo (en bytes)
#define ADC_FRAME_SIZE        1024                      // Tamaño del frame de conversión (en bytes), debe ser múltiplo de SOC_ADC_DIGI_DATA_BYTES_PER_CONV

static adc_continuous_handle_t adc_handle = NULL;
static uint8_t adc_iniciado = 0;

void adc_init(void)
{
    if (adc_handle != NULL) // Verifica si el ADC ya se inicializó para evitar inicializaciones múltiples
    {
        return;
    }
    // Configuración del ADC continuo
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = ADC_POOL_SIZE,
        .conv_frame_size = ADC_FRAME_SIZE,
    };

    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_handle));
    // Configuración de los patrones de conversión para los canales del ADC
    adc_digi_pattern_config_t adc_pattern[2] = {
        {
            .atten = ADC_ATTEN,
            .channel = ADC_CHANNEL_SIGNAL,
            .unit = ADC_UNIT,
            .bit_width = ADC_BITWIDTH,
        },
        {
            .atten = ADC_ATTEN,
            .channel = ADC_CHANNEL_TRIGGER,
            .unit = ADC_UNIT,
            .bit_width = ADC_BITWIDTH,
        }
    };
    // Configuración de la estructura de configuración del ADC continuo
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = ADC_SAMPLE_FREQ_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1, 
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2, 
        .pattern_num = 2,
        .adc_pattern = adc_pattern,
    };

    ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &dig_cfg));
}

esp_err_t adc_start(void)
{
    esp_err_t err;

    if (adc_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;   // El ADC no ha sido inicializado
    }

    if (adc_iniciado == 1)
    {
        return ESP_OK;                 // El ADC ya está iniciado
    }

    err = adc_continuous_start(adc_handle);     // Inicia la conversión continua del ADC mediante el driver 

    if (err == ESP_OK)      // Si la operación fue exitosa, actualiza el estado de inicio del ADC
    {
        adc_iniciado = 1;       
    }

    return err;
}

esp_err_t adc_stop(void)
{
    esp_err_t err;

    if (adc_handle == NULL)
    {
        return ESP_ERR_INVALID_STATE;  // El ADC no ha sido inicializado
    }

    if (adc_iniciado == 0)
    {
        return ESP_OK;
    }

    err = adc_continuous_stop(adc_handle);      // Detiene la conversión continua del ADC mediante el driver

    if (err == ESP_OK)
    {
        adc_iniciado = 0;       // Actualiza el estado del ADC a no iniciado
    }

    return err;
}

esp_err_t adc_leer_muestras(
    adc_muestra_t *muestras,
    uint16_t max_muestras,
    uint16_t *cantidad_muestras
)

{
    static uint8_t dma_buf[ADC_FRAME_SIZE];     // Buffer para almacenar temporalmente los datos leídos del ADC

    uint32_t out_len = 0;       // Guardar la cantidad de bytes leídos del ADC
    uint16_t indice = 0;        // Índice para recorrer el arreglo de muestras, indica la proxima posición disponible para almacenar una nueva muestra
    esp_err_t ret;              // Almacena el resultado de las operaciones del driver del ADC

    if (muestras == NULL || cantidad_muestras == NULL)
    {
        return ESP_ERR_INVALID_ARG;  // Verifica si los punteros de entrada son válidos
    }

    *cantidad_muestras = 0;     // Inicializao la cantidad de muestras leídas a cero

    // Leer un frame de datos del ADC continuo mediante el driver
    // Timeout bajo para evitar bloqueos 
    ret = adc_continuous_read(adc_handle, dma_buf, sizeof(dma_buf), &out_len, 10);

    if (ret != ESP_OK)
    {
        return ret;     // Si ocurre un error durante la lectura, retorna el código de error
    }

    // Recorro el buffer de datos leídos byte por byte
    for (uint32_t i = 0; i < out_len; i += SOC_ADC_DIGI_RESULT_BYTES)
    {
        adc_digi_output_data_t *p = (adc_digi_output_data_t *)&dma_buf[i];      //Interpreta los bytes leídos como una estructura de datos del ADC

        if (indice >= max_muestras) // No escribir mas muestras que la capacidad del arreglo 
        {
            break;
        }

        if (p->type2.unit == 0)     // Verifica si la unidad del ADC es la correcta 
        {
            if (p->type2.channel == ADC_CHANNEL_SIGNAL)     // Verifica si el canal del ADC es el canal de señal
            {
                muestras[indice].canal = ADC_CANAL_SENAL;   // Asigna el canal de señal a la muestra
                muestras[indice].raw = p->type2.data;       // Copia el resultado de 12 bits del ADC a la muestra
                indice++;                                   // Incrementa el índice para la siguiente muestra
            }
            else if (p->type2.channel == ADC_CHANNEL_TRIGGER)
            {
                muestras[indice].canal = ADC_CANAL_TRIGGER;
                muestras[indice].raw = p->type2.data;
                indice++;
            }
        }
    }

    *cantidad_muestras = indice;        // Informar muestras copiadas al arreglo 

    return ESP_OK;
}

uint16_t adc_raw_a_mv(uint16_t raw)
{
    return (uint16_t)(((uint32_t)raw * ADC_VREF_MV) / ADC_RAW_MAX); // uint32 para evitar overflow en la multiplicación
}
