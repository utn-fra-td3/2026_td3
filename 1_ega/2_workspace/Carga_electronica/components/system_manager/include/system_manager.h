#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

// Definimos nuestros estados lógicos
typedef enum {
    MODE_CC, // Corriente Constante
    MODE_CR, // Resistencia Constante
    MODE_CV  // Voltaje Constante
} system_mode_t;

typedef enum {
    ALARM_NONE,
    ALARM_OVP, // Over-Voltage Protection
    ALARM_OCP  // Over-Current Protection
} alarm_type_t;

typedef enum {
    SYSMAN_MSG_MODE_CHANGE,
    SYSMAN_MSG_SETPOINT_CHANGE,
    SYSMAN_MSG_ADC_UPDATE,
    SYSMAN_MSG_ALARM_LIMIT_CHANGE,
    SYSMAN_MSG_ARM_ALARMS,
} sysman_msg_type_t;

typedef struct {
    sysman_msg_type_t type;
    union {
        system_mode_t new_mode;
        float new_setpoint; // Voltaje, Corriente o Resistencia (dependiendo del modo actual)
        
        struct {
            float voltage;
            float current;
        } adc;
        
        struct {
            alarm_type_t type;
            float new_limit;
        } alarm_limit;
    } data;
} sysman_msg_t;

// Estructura que el PID leerá para saber qué regular
typedef struct {
    system_mode_t mode;
    float setpoint; // Puede ser Amperios (CC) u Ohmios (CR)
    bool force_stop; // Si es true, el PID apaga la carga inmediatamente
} pid_config_t;

void task_system_manager(void *pvParameters);

#endif