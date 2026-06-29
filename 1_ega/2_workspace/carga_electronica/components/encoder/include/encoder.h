#ifndef ENCODER_PCNT_H
#define ENCODER_PCNT_H

#define ENCODER_PIN_SW      6   
#define CONFIG_BUTTON_PIN   3   

// Eventos de la cola
#define EVENT_SW_PRESSED     1  // Cambiar dígito
#define EVENT_CONFIG_PRESSED  2  // Entrar/Salir Config

void task_encoder_read(void *pvParameters);

#endif