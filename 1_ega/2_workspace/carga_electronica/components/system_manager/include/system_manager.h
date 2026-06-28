#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

// Definimos nuestros estados lógicos
typedef enum {
    MODE_CC, // Corriente Constante
    MODE_CR  // Resistencia Constante
} system_mode_t;

// Estructura que el PID leerá para saber qué regular
typedef struct {
    system_mode_t mode;
    float setpoint; 
} pid_config_t;

void task_system_manager(void *pvParameters);

#endif