#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2c.h"
#include "esp_log.h"

#include "../components/mcp4725/include/mcp4725.h"
#include "../components/encoder/include/encoder.h"
#include "../components/eeprom/include/eeprom.h"
#include "../components/oled/include/oled.h"

// --- CONFIGURACIÓN DE PINES I2C ---
#define I2C_MASTER_SDA_IO           8      // Pin para datos (SDA)
#define I2C_MASTER_SCL_IO           9      // Pin para reloj (SCL)

#define I2C_MASTER_NUM              I2C_NUM_0 // Usaremos el puerto I2C 0
#define I2C_MASTER_FREQ_HZ          400000    // Frecuencia a 400kHz 
#define I2C_MASTER_TX_BUF_DISABLE   0         // El maestro no usa buffer TX
#define I2C_MASTER_RX_BUF_DISABLE   0         // El maestro no usa buffer RX

// --- STRUCT DAC ---
typedef struct {
    uint16_t dac_value;
    uint16_t max_limit;
    uint16_t min_limit;
} system_data_t;

// --- COLAS DE COMUNICACION ---
QueueHandle_t display_queue = NULL;
QueueHandle_t eeprom_queue = NULL;

static const char *TAG = "MAIN";

// Handle de la cola de FreeRTOS
static QueueHandle_t dac_queue = NULL;

// Declaro Mutex para i2c
SemaphoreHandle_t i2c_mutex = NULL;


static pcnt_unit_handle_t pcnt_unit = NULL;

// --- INICIALIZACIONES ---
/**
 * @brief Inicializa el puerto I2C en modo Maestro
 */
esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE, // resistencia pull-up interna
        .scl_pullup_en = GPIO_PULLUP_ENABLE, // resistencia pull-up interna
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    // Configurar los parámetros estructurales
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        return err;
    }

    // Instalar el driver del puerto I2C
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 
                              I2C_MASTER_RX_BUF_DISABLE, 
                              I2C_MASTER_TX_BUF_DISABLE, 0);
}

static void encoder_init_pcnt(void) {
    pcnt_unit_config_t unit_config = {
        .high_limit = ENCODER_HIGH_LIMIT,
        .low_limit = ENCODER_LOW_LIMIT,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    // Configurar filtro de ruido por hardware 
    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 1000 }; // 1us
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    // Configurar Canal A
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = ENCODER_PIN_A,
        .level_gpio_num = ENCODER_PIN_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    // Configurar Canal B
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = ENCODER_PIN_B,
        .level_gpio_num = ENCODER_PIN_A,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    // Definir comportamiento del conteo en cuadratura (Modo X2)
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    // Activar y arrancar el contador
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
}


// --- TAREAS ---

/**
 * @brief Control del DAC
 */
void task_dac_control(void *pvParameters) {

    //ACA HARDCODEO para prueba
    uint16_t dac_target_value = 512; 

    ESP_LOGI(TAG, "Iniciando Tarea de Control del DAC...");

    while (1) {

        // Pido el mutex. Espero max 50ms
        if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(50)) == pdTRUE){
            // Escribo el valor en el dac
            mcp4725_set_voltage(dac_target_value);

            xSemaphoreGive(i2c_mutex); // Libero el mutex
        }else{
            ESP_LOGW(TAG, "Bus I2C ocupado. No se pudo actualizar el DAC.");
        }     

        // Envio el valor a la cola, máximo 10ms
        if (xQueueSend(dac_queue, &dac_target_value, pdMS_TO_TICKS(10)) != pdPASS) {
            ESP_LOGW(TAG, "Cola llena. No se pudo enviar el dato a la consola.");
        }

        // Estado blocked por 1000 ms
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}

void task_encoder_read(void *pvParameters) {
    int pulse_count = 0;
    int last_pulse_count = -1;
    system_data_t current_state = { .dac_value = 0, .max_limit = 3500, .min_limit = 200 };

    encoder_init_pcnt();

    while (1) {
        // Leer el valor actual acumulado por el hardware
        pcnt_unit_get_count(pcnt_unit, &pulse_count);

        // --- CLAMPING POR SOFTWARE ---
        if (pulse_count < 0) {
            pulse_count = 0;
            pcnt_unit_clear_count(pcnt_unit); // Reseteo el hardware a 0
        } else if (pulse_count > 4095) {
            pulse_count = 4095;
            pcnt_unit_clear_count(pcnt_unit); // Limito la variable a 4095
        }

        if (pulse_count != last_pulse_count) {
            current_state.dac_value = (uint16_t)pulse_count;
            
            ESP_LOGI("ENCODER", "Giro detectado | Valor actual: %d", pulse_count);

            // Actualizar el DAC inmediatamente si es necesario, o enviarlo a la tarea del DAC

            // Enviar datos actualizados a la Pantalla y a la EEPROM
            xQueueSend(display_queue, &current_state, 0);
            xQueueSend(eeprom_queue, &current_state, 0);

            last_pulse_count = pulse_count;
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // Revisa el cambio de hardware cada 50ms
    }
}

/*
// Este codigo lo hago para probar el encoder
void task_encoder_read(void *pvParameters) {
    int pulse_count = 0;
    
    // 1. Forzamos la inicialización
    encoder_init_pcnt(); 
    
    // LOG DE CONTROL 1: Si no ves esto al encender, la tarea NO está creada en app_main
    ESP_LOGW("ENCODER_DEBUG", "¡LA TAREA ENCODER ARRANCÓ CON ÉXITO!");

    while (1) {
        // Leemos el hardware
        pcnt_unit_get_count(pcnt_unit, &pulse_count);
        
        // LOG DE CONTROL 2: Imprime CADA 1 SEGUNDO, muévase o no el encoder
        ESP_LOGI("ENCODER_DEBUG", "Latido de tarea vivo | Valor actual del chip: %d", pulse_count);

        // Bajamos temporalmente el delay a 1 segundo para pruebas
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
*/

void task_eeprom_manager(void *pvParameters) {
    system_data_t data_to_save;
    system_data_t last_saved_data = {0};
    bool needs_saving = false;

    eeprom_queue = xQueueCreate(1, sizeof(system_data_t));

    while (1) {
        // Esperamos un dato de la cola por 3000ms 
        if (xQueueReceive(eeprom_queue, &data_to_save, pdMS_TO_TICKS(3000)) == pdTRUE) {
            // Llegó un cambio del encoder. No guardamos todavía, solo tomamos nota.
            needs_saving = true;
        } else {
            // Pasaron 3000ms, Es seguro guardar en la EEPROM
            if (needs_saving && (data_to_save.dac_value != last_saved_data.dac_value)) {
                
                ESP_LOGI("EEPROM", "Guardando Setpoint de seguridad en EEPROM: %d", data_to_save.dac_value);
                
                // Protegemos el bus I2C compartiendo con el Mutex
                if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE) {
                    
                    // Separamos los 16 bits del dac_value en dos bytes para la EEPROM
                    eeprom_write_byte(0x0010, (uint8_t)(data_to_save.dac_value >> 8));
                    eeprom_write_byte(0x0011, (uint8_t)(data_to_save.dac_value & 0xFF));
                    
                    xSemaphoreGive(i2c_mutex);
                    
                    last_saved_data = data_to_save;
                    needs_saving = false; // Guardado exitoso
                }
            }
        }
    }
}

// Codigo display (Faltan Pruebas)
void task_display_update(void *pvParameters) {
    system_data_t state_to_display;
    
    // Inicializamos la pantalla
    // Tomamos el Mutex
    if (xSemaphoreTake(i2c_mutex, portMAX_DELAY) == pdTRUE) {
        oled_init_minimal();
        oled_clear();
        xSemaphoreGive(i2c_mutex);
    }

    ESP_LOGI("SCREEN", "Pantalla OLED Baremetal Iniciada");

    while (1) {
        // espero hasta que el encoder o el sistema manden un dato nuevo
        if (xQueueReceive(display_queue, &state_to_display, portMAX_DELAY) == pdTRUE) {
            
            // Llego un dato nuevo. Pedimos semaforo para usar el bus I2C
            if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                
                // --- INICIO ZONA CRÍTICA I2C ---
                
                // Limpiamos solo el renglón donde vamos a escribir para no hacer parpadear toda la pantalla
                oled_set_cursor(0, 3); // Columna 0, Renglón 3 (mitad de pantalla)
                for(int i=0; i<30; i++) oled_send_data(0x00); // Borramos el número anterior
                
                // Posicionamos el cursor nuevamente y escribimos el nuevo valor del DAC
                oled_set_cursor(0, 3); 
                oled_print_number(state_to_display.dac_value);
                
                // --- FIN ZONA CRÍTICA I2C ---
                
                // Soltamos el bus inmediatamente
                xSemaphoreGive(i2c_mutex);
                
            } else {
                ESP_LOGW("SCREEN", "I2C ocupado, se perdió un frame de pantalla");
            }
        }
    }
}

/**
 * @brief Impresión por Consola (Logger)
 */
void task_console_logger(void *pvParameters) {
    uint16_t received_value = 0;
    ESP_LOGI(TAG, "Iniciando Tarea de Consola...");

    while (1) {
        // xQueueReceive bloquea la tarea indefinidamente 
        // espero hasta que task_dac_control ponga un dato en la cola
        if (xQueueReceive(dac_queue, &received_value, portMAX_DELAY) == pdTRUE) {
            
            // Calculamos el voltaje teórico para mostrarlo en consola
            float voltage = (received_value * 3.3f) / 4096.0f;

            ESP_LOGI(TAG, "DAC UPDATE -> Digital: %d | Voltaje aprox: %.2f V", 
                     received_value, voltage);
        }
    }
}


void app_main(void) {
    ESP_LOGI("MAIN", "Iniciando Carga Electrónica...");

    // Inicializar el hardware (Bus I2C)
    esp_err_t err = i2c_master_init();
    if (err == ESP_OK) {
        ESP_LOGI("MAIN", "Bus I2C inicializado correctamente");
    } else {
        ESP_LOGE("MAIN", "Error inicializando I2C: %s", esp_err_to_name(err));
        return; // Si el I2C falla, detenemos el programa acá
    }
    // Creo el Mutex
    i2c_mutex = xSemaphoreCreateMutex();
    
    //  Crear la cola de comunicación
    dac_queue = xQueueCreate(5, sizeof(uint16_t));
    if (dac_queue == NULL){
        ESP_LOGE("MAIN", "Error: No se pudo crear la cola por falta de memoria.");
        while(1); // Frena el programa aquí para que te des cuenta
        }

    //  Crear las tareas de FreeRTOS
    if (dac_queue != NULL) {
        xTaskCreate(task_dac_control, "DAC_Control", 4096, NULL, 5, NULL);
        xTaskCreate(task_encoder_read, "Encoder_Task", 4096, NULL, 6, NULL); 
        xTaskCreate(task_display_update, "Display_Task", 4096, NULL, 4, NULL);
        xTaskCreate(task_eeprom_manager, "EEPROM_Task", 4096, NULL, 3, NULL); 
        xTaskCreate(task_console_logger, "Console_Logger", 4096, NULL, 3, NULL);
    }
}
