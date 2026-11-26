#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Archivos de cabecera del proyecto
#include "menu.h"
#include "encoder.h"          // Eventos físicos (ENCODER_UP, ENCODER_DOWN, ENCODER_CLICK)
#include "tipos_sistema.h"    // colas_sistema_t y param_barrido_t
#include "storage.h"          // NVS guardar/cargar
#include "guardiana.h"        // imprimir_consola()
#include "oled_ssd1306.h"     // Pantalla OLED SSD1306

typedef enum {
    PANTALLA_PRINCIPAL,
    SUBMENU_FRECUENCIA,
    EDITANDO_F_INI,
    EDITANDO_F_FIN,
    EDITANDO_SALTO,
    EDITANDO_PASO,
    EDITANDO_TIEMPO,
    BARRIDO_EN_CURSO
} estado_menu_t;

// Pasos de barrido disponibles (para el algoritmo de medición punto a punto)
const int PASOS_DISPONIBLES[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000};
#define TOTAL_PASOS 10

// Saltos disponibles para editar frecuencia inicial/final

const int SALTOS_FREQ[] = {1,10, 100, 1000, 10000, 100000};
const char* const ETIQUETAS_SALTO[] = {"1 HZ", "10 HZ", "100 HZ", "1 KHZ", "10 KHZ", "100 KHZ"};
#define TOTAL_SALTOS 6

#define FREQ_MAX_HZ 1000000
#define FREQ_MIN_HZ 1
#define TIEMPO_MIN_MS 50
#define TIEMPO_MAX_MS 500

static void refrescar_oled(estado_menu_t estado, int cursor_prin, int cursor_sub, int f_ini, int f_fin, int salto_idx, int paso, int tiempo) {
    oled_clear();
    
    static char b_linea1[32];
    static char b_linea2[32];
    static char b_linea3[32];
    static char b_linea4[32];

    // Título visible en pantalla OLED
    oled_draw_string(10, 0, "--- ANALIZADOR ---", false);

    if (estado == BARRIDO_EN_CURSO) {
        oled_draw_string(15, 4, ">> MIDIENDO... <<", false);
    } 
    else if (estado == SUBMENU_FRECUENCIA || estado == EDITANDO_F_INI || estado == EDITANDO_F_FIN || estado == EDITANDO_SALTO) {
        // Línea 1: Frecuencia Inicial
        snprintf(b_linea1, sizeof(b_linea1), "%c F. INI: %d HZ", 
            (estado == EDITANDO_F_INI ? '#' : (cursor_sub == 0 ? '>' : ' ')), f_ini);
        oled_draw_string(0, 2, b_linea1, false);

        // Línea 2: Frecuencia Final
        snprintf(b_linea2, sizeof(b_linea2), "%c F. FIN: %d HZ", 
            (estado == EDITANDO_F_FIN ? '#' : (cursor_sub == 1 ? '>' : ' ')), f_fin);
        oled_draw_string(0, 3, b_linea2, false);

        // Línea 3: Selector de Salto (10 HZ, 100 HZ, 1 KHZ, 10 KHZ, 100 KHZ)
        snprintf(b_linea3, sizeof(b_linea3), "%c SALTO: [%s]", 
            (estado == EDITANDO_SALTO ? '#' : (cursor_sub == 2 ? '>' : ' ')), ETIQUETAS_SALTO[salto_idx]);
        oled_draw_string(0, 4, b_linea3, false);

        // Línea 4: Opción para volver a la pantalla principal
        snprintf(b_linea4, sizeof(b_linea4), "%c VOLVER", (cursor_sub == 3 ? '>' : ' '));
        oled_draw_string(0, 6, b_linea4, false);
    } 
    else { 
        // Pantalla Principal
        snprintf(b_linea1, sizeof(b_linea1), "%c FRECUENCIA", (cursor_prin == 0 ? '>' : ' '));
        oled_draw_string(0, 2, b_linea1, false);

        snprintf(b_linea2, sizeof(b_linea2), "%c PASO: %d", (estado == EDITANDO_PASO ? '#' : (cursor_prin == 1 ? '>' : ' ')), paso);
        oled_draw_string(0, 3, b_linea2, false);

        snprintf(b_linea3, sizeof(b_linea3), "%c TIEMPO: %d MS", (estado == EDITANDO_TIEMPO ? '#' : (cursor_prin == 2 ? '>' : ' ')), tiempo);
        oled_draw_string(0, 4, b_linea3, false);

        snprintf(b_linea4, sizeof(b_linea4), "%c INICIAR BARRIDO", (cursor_prin == 3 ? '>' : ' '));
        oled_draw_string(0, 6, b_linea4, false); 
    }
    
    oled_update();
}

void vTareaMenu(void *pvParameters) {
    colas_sistema_t *colas = (colas_sistema_t *) pvParameters;
    encoder_event_t event;
    
    // Variables de estado del sistema por defecto
    int freq_inicial = 100; 
    int freq_final = 10000;
    bool freq_inicial_seteada = false; 
    bool freq_final_seteada = false;
    int indice_paso_actual = 0; 
    int tiempo_actual = 500;
    
    int indice_salto_freq = 3; // Por defecto: Salto de 1 KHZ
    int cursor_principal = 0; 
    int cursor_submenu = 0;
    estado_menu_t estado_actual = PANTALLA_PRINCIPAL;

    cargar_configuracion(&freq_inicial, &freq_final, &indice_paso_actual, &tiempo_actual, &freq_inicial_seteada, &freq_final_seteada);
    if (tiempo_actual < TIEMPO_MIN_MS) {
        tiempo_actual = TIEMPO_MIN_MS;
    }
    if (tiempo_actual > TIEMPO_MAX_MS) {
        tiempo_actual = TIEMPO_MAX_MS;
    }
    if (indice_paso_actual < 0 || indice_paso_actual >= TOTAL_PASOS) {
        indice_paso_actual = 0; 
    }

    #define IMPRIMIR_PANTALLA() do { \
        int paso_val = PASOS_DISPONIBLES[indice_paso_actual]; \
        int cursor_json = (estado_actual == SUBMENU_FRECUENCIA || estado_actual == EDITANDO_F_INI || estado_actual == EDITANDO_F_FIN || estado_actual == EDITANDO_SALTO) ? cursor_submenu : cursor_principal; \
        imprimir_consola("{\"tipo\":\"menu\", \"cursor\":%d, \"estado\":%d, \"f_ini\":%d, \"f_fin\":%d, \"salto\":%d, \"paso\":%d, \"tiempo\":%d, \"set_ini\":%d, \"set_fin\":%d}\n", \
            cursor_json, (int)estado_actual, freq_inicial, freq_final, SALTOS_FREQ[indice_salto_freq], paso_val, tiempo_actual, \
            freq_inicial_seteada ? 1 : 0, freq_final_seteada ? 1 : 0); \
        refrescar_oled(estado_actual, cursor_principal, cursor_submenu, freq_inicial, freq_final, indice_salto_freq, paso_val, tiempo_actual); \
    } while(0)

    IMPRIMIR_PANTALLA();

    while(1) {
        if (xQueueReceive(colas->q_encoder, &event, portMAX_DELAY) == pdTRUE) {
            bool actualizar = false;
            int paso_val = PASOS_DISPONIBLES[indice_paso_actual];
            int salto_val = SALTOS_FREQ[indice_salto_freq];

            switch (estado_actual) {
                case PANTALLA_PRINCIPAL:
                    if (event == ENCODER_UP) { 
                        cursor_principal = (cursor_principal + 1) % 4; 
                        actualizar = true; 
                    }
                    else if (event == ENCODER_DOWN) { 
                        cursor_principal = (cursor_principal - 1 + 4) % 4; 
                        actualizar = true; 
                    }
                    else if (event == ENCODER_CLICK) {
                        if (cursor_principal == 0) { 
                            estado_actual = SUBMENU_FRECUENCIA; 
                            cursor_submenu = 0; 
                        }
                        else if (cursor_principal == 1) { 
                            estado_actual = EDITANDO_PASO; 
                        }
                        else if (cursor_principal == 2) { 
                            estado_actual = EDITANDO_TIEMPO; 
                        }
                        else if (cursor_principal == 3) {
                            if (freq_inicial >= freq_final) {
                                imprimir_consola("{\"tipo\":\"error\", \"msg\":\"F. Inicial debe ser menor\"}\n");
                                vTaskDelay(pdMS_TO_TICKS(1000));
                            } else {
                                imprimir_consola("{\"tipo\":\"info\", \"msg\":\"Iniciando Barrido\"}\n");
                                param_barrido_t parametros = {freq_inicial, freq_final, paso_val, tiempo_actual};
                                xQueueSend(colas->q_comandos_barrido, &parametros, 0);
                                estado_actual = BARRIDO_EN_CURSO;
                            }
                        }
                        actualizar = true;
                    }
                    break;

                case SUBMENU_FRECUENCIA:
                    if (event == ENCODER_UP) { 
                        cursor_submenu = (cursor_submenu + 1) % 4; 
                        actualizar = true; 
                    }
                    else if (event == ENCODER_DOWN) { 
                        cursor_submenu = (cursor_submenu - 1 + 4) % 4; 
                        actualizar = true; 
                    }
                    else if (event == ENCODER_CLICK) {
                        if (cursor_submenu == 0) { estado_actual = EDITANDO_F_INI; }
                        else if (cursor_submenu == 1) { estado_actual = EDITANDO_F_FIN; }
                        else if (cursor_submenu == 2) { estado_actual = EDITANDO_SALTO; }
                        else if (cursor_submenu == 3) { estado_actual = PANTALLA_PRINCIPAL; }
                        actualizar = true;
                    }
                    break;

                case EDITANDO_F_INI:
                    // Al girar se suma o resta según el salto seleccionado
                    if (event == ENCODER_UP) { 
                        if (freq_inicial + salto_val <= FREQ_MAX_HZ) freq_inicial += salto_val; 
                        else freq_inicial = FREQ_MAX_HZ;
                        actualizar = true; 
                    }
                    else if (event == ENCODER_DOWN) { 
                        if (freq_inicial - salto_val >= FREQ_MIN_HZ) freq_inicial -= salto_val; 
                        else freq_inicial = FREQ_MIN_HZ;
                        actualizar = true; 
                    }
                    else if (event == ENCODER_CLICK) { 
                        // Haciendo click confirma, guarda en NVS y vuelve al submenú
                        freq_inicial_seteada = true;
                        estado_actual = SUBMENU_FRECUENCIA; 
                        guardar_configuracion(freq_inicial, freq_final, indice_paso_actual, tiempo_actual, freq_inicial_seteada, freq_final_seteada);
                        actualizar = true; 
                    }
                    break;

                case EDITANDO_F_FIN:
                    if (event == ENCODER_UP) { 
                        if (freq_final + salto_val <= FREQ_MAX_HZ) freq_final += salto_val; 
                        else freq_final = FREQ_MAX_HZ;
                        actualizar = true; 
                    }
                    else if (event == ENCODER_DOWN) { 
                        if (freq_final - salto_val >= FREQ_MIN_HZ) freq_final -= salto_val; 
                        else freq_final = FREQ_MIN_HZ;
                        actualizar = true; 
                    }
                    else if (event == ENCODER_CLICK) { 
                        // Haciendo click confirma, guarda en NVS y vuelve al submenú
                        freq_final_seteada = true;
                        estado_actual = SUBMENU_FRECUENCIA; 
                        guardar_configuracion(freq_inicial, freq_final, indice_paso_actual, tiempo_actual, freq_inicial_seteada, freq_final_seteada);
                        actualizar = true; 
                    }
                    break;

                case EDITANDO_SALTO:
                    // Entre 1HZ, 10 HZ, 100 HZ, 1 KHZ, 10 KHZ, 100 KHZ
                    if (event == ENCODER_UP) { 
                        indice_salto_freq = (indice_salto_freq + 1) % TOTAL_SALTOS; 
                        actualizar = true; 
                    }
                    else if (event == ENCODER_DOWN) { 
                        indice_salto_freq = (indice_salto_freq - 1 + TOTAL_SALTOS) % TOTAL_SALTOS; 
                        actualizar = true; 
                    }
                    else if (event == ENCODER_CLICK) { 
                        // Confirma el salto elegido y regresa al submenú de frecuencias
                        estado_actual = SUBMENU_FRECUENCIA; 
                        actualizar = true; 
                    }
                    break;

                case EDITANDO_PASO:
                    if (event == ENCODER_UP) { 
                        indice_paso_actual = (indice_paso_actual + 1) % TOTAL_PASOS; 
                        actualizar = true; 
                    }
                    else if (event == ENCODER_DOWN) { 
                        indice_paso_actual = (indice_paso_actual - 1 + TOTAL_PASOS) % TOTAL_PASOS; 
                        actualizar = true; 
                    }
                    else if (event == ENCODER_CLICK) { 
                        estado_actual = PANTALLA_PRINCIPAL; 
                        guardar_configuracion(freq_inicial, freq_final, indice_paso_actual, tiempo_actual, freq_inicial_seteada, freq_final_seteada);
                        actualizar = true; 
                    }
                    break;

                case EDITANDO_TIEMPO:
                    if (event == ENCODER_UP) { 
                       if(tiempo_actual + 50 <= TIEMPO_MAX_MS)
                        tiempo_actual += 50;
                        else tiempo_actual = TIEMPO_MAX_MS; 
                        actualizar = true;
                    }
                    else if (event == ENCODER_DOWN) { 
                        if (tiempo_actual - 50 >= TIEMPO_MIN_MS) 
                        tiempo_actual -= 50; 
                        else tiempo_actual = TIEMPO_MIN_MS;
                        actualizar = true; 
                    }
                    else if (event == ENCODER_CLICK) { 
                        estado_actual = PANTALLA_PRINCIPAL; 
                        guardar_configuracion(freq_inicial, freq_final, indice_paso_actual, tiempo_actual, freq_inicial_seteada, freq_final_seteada);
                        actualizar = true; 
                    }
                    break;

                case BARRIDO_EN_CURSO:
                    if (event == EVENTO_BARRIDO_FIN || event == ENCODER_CLICK) {
                        estado_actual = PANTALLA_PRINCIPAL;
                        actualizar = true;
                    }
                    break;
            }
            
            if (actualizar) {
                IMPRIMIR_PANTALLA();
            }
        }
    }
}

