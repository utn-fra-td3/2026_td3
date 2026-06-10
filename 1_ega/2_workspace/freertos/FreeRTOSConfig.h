#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/**
 * @file FreeRTOSConfig.h
 * @brief Configuración básica para FreeRTOS en un RP2350
 * @author Fabrizio Carlassara - UTN FRA - Laboratorio de Sistemas Embebidos
 * 
 * Algunas lecturas adicionales para mayor customización:
 * 
 * - [1] Barry, R. "Mastering the FreeRTOS Real Time Kernel", https://github.com/FreeRTOS/FreeRTOS-Kernel-Book/releases/download/V1.1.0/Mastering-the-FreeRTOS-Real-Time-Kernel.v1.1.0.pdf
 * - [2] AWS, "The FreeRTOS™ Reference Manual", https://www.freertos.org/media/2018/FreeRTOS_Reference_Manual_V10.0.0.pdf
 * - [3] AWS, Customization, https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/02-Customization
 * - [4] Gaurav Aggarwal, Using FreeRTOS on ARMv8-M Microcontrollers, https://www.freertos.org/Community/Blogs/2020/using-freertos-on-armv8-m-microcontrollers
 */

// Handlers usados por el SDK de Raspberry Pi Pico

/**
 * @brief Excepción solicitada por la aplicación para bootear el RTOS
 * @see Raspberry Pi, Raspberry Pi Pico SDK, https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/cmsis/include/cmsis/rename_exceptions.h
 * @see ARM, "Armv6-M Architecture Reference Manual", https://developer.arm.com/documentation/ddi0419/latest/
 */
#define vPortSVCHandler isr_svcall
/**
 * @brief Excepción solicitada por la aplicación en cada cambio de contexto del RTOS
 * @see Raspberry Pi, Raspberry Pi Pico SDK, https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/cmsis/include/cmsis/rename_exceptions.h
 * @see ARM, "Armv6-M Architecture Reference Manual", https://developer.arm.com/documentation/ddi0419/latest/
 */
#define xPortPendSVHandler  isr_pendsv
/**
 * @brief Interrupción de SysTick del RTOS
 * @see Raspberry Pi, Raspberry Pi Pico SDK, https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/cmsis/include/cmsis/rename_exceptions.h
 * @see ARM, "Armv6-M Architecture Reference Manual", https://developer.arm.com/documentation/ddi0419/latest/
 */
#define xPortSysTickHandler isr_systick

// Configuración básica del sistema

/**
 * @brief Planificación por prioridad de tareas
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 3.12
 */
#define configUSE_PREEMPTION  1
/**
 * @brief Planificación de tareas con reparto parejo de tiempo
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 3.12
 */
#define configUSE_TIME_SLICING  1
/**
 * @brief Tiempo de tick de 1 ms
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 3.6
 */
#define configTICK_RATE_HZ  1000
/**
 * @brief Máxima prioridad disponible al crear una tarea (configMAX_PRIORITIES - 1)
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 3.5
 */
#define configMAX_PRIORITIES  6
/**
 * @brief Cantidad de palabras reservadas para el stack de la tarea IDLE.
 * @note Menos que esto no es recomendable asignar para otra tarea
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 3.4
 */
#define configMINIMAL_STACK_SIZE  128
/**
 * @brief Cantidad de bytes de memoria dinámica disponibles para FreeRTOS
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 2.2
 */
#define configTOTAL_HEAP_SIZE 16364
/**
 * @brief Cantidad de caracteres que se le pueden asignar al nombre de una tarea para debugging
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 2.2
 */
#define configMAX_TASK_NAME_LEN 16
/**
 * @brief Tamaño de la variable que guarda el valor de ticks del RTOS
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 1.5
 */
#define configUSE_16_BIT_TICKS  0
/**
 * @brief Permite que la tarea IDLE ceda su time slice si hay otra tarea lista
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 3.12
 */
#define configIDLE_SHOULD_YIELD 1
/**
 * @brief Usa la tarea IDLE por defecto de FreeRTOS
 * @see AWS, "Hook Functions", https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/12-Hook-functions
 */
#define configUSE_IDLE_HOOK 0
/**
 * @brief Usa la implementación de interrupción de Tick de FreeRTOS
 * @see AWS, "Hook Functions", https://www.freertos.org/Documentation/02-Kernel/02-Kernel-features/12-Hook-functions
 */
#define configUSE_TICK_HOOK 0
/**
 * @brief Nivel de interrupción del NVIC desde el que APIs de interrupciones de FreeRTOS pueden ser llamadas
 * @note También puede encontrarse como configMAX_SYSCALL_INTERRUPT_PRIORITY
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 6.8
 */
#define configMAX_API_CALL_INTERRUPT_PRIORITY 16
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  16

/**
 * @brief Implementación de assert para atrapar errores
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 11.2
 */
#define configASSERT( x ) if( ( x ) == 0 ) { portDISABLE_INTERRUPTS(); for( ;; ); }

// Particularidades de Cortex-M33 (ARMv8-M)

/**
 * @brief Sin protección de memoria o restricciones de acceso
 * @see Gaurav Aggarwal, Using FreeRTOS on ARMv8-M Microcontrollers, https://www.freertos.org/Community/Blogs/2020/using-freertos-on-armv8-m-microcontrollers
 */
#define configENABLE_MPU  0
/**
 * @brief Toda la zona de memoria para la aplicación es no segura
 * @see Gaurav Aggarwal, Using FreeRTOS on ARMv8-M Microcontrollers, https://www.freertos.org/Community/Blogs/2020/using-freertos-on-armv8-m-microcontrollers
 */
#define configENABLE_TRUSTZONE  0
/**
 * @brief Habilita a FreeRTOS a correr en la zona de memoria segura si el hardware no permite deshabilitar TrustZone
 * @see Gaurav Aggarwal, Using FreeRTOS on ARMv8-M Microcontrollers, https://www.freertos.org/Community/Blogs/2020/using-freertos-on-armv8-m-microcontrollers
 */
#define configRUN_FREERTOS_SECURE_ONLY  1
/**
 * @brief Habilita el uso de la unidad de punto flotante por hardware
 * @see Gaurav Aggarwal, Using FreeRTOS on ARMv8-M Microcontrollers, https://www.freertos.org/Community/Blogs/2020/using-freertos-on-armv8-m-microcontrollers
 */
#define configENABLE_FPU  1

// Software Timers

/**
 * @brief Habilita el uso de Software Timers usados en el port del RP2040/RP2350
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 5.1
 */
#define configUSE_TIMERS              1
/**
 * @brief Máxima prioridad disponible para el Timer Service Task
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 5.4
 */
#define configTIMER_TASK_PRIORITY     (configMAX_PRIORITIES - 1)
/**
 * @brief Máxima cantidad de elementos para la cola de comandos del Timer Service Task
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 5.4
 */
#define configTIMER_QUEUE_LENGTH      10
/**
 * @brief Stack del Timer Service Task
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 5.4
 */
#define configTIMER_TASK_STACK_DEPTH  configMINIMAL_STACK_SIZE

// Semáforos, Mutex, Counting

/**
 * @brief Habilita el uso de semáforos mutex
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 7.3
 */
#define configUSE_MUTEXES 1

/**
 * @brief Habilita el uso de semáforos counting
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 6.5
 */
#define configUSE_COUNTING_SEMAPHORES 1

// Funciones opcionales utilizadas por las tareas

/**
 * @brief Habilita el API vTaskDelay
 * @see AWS, "The FreeRTOS™ Reference Manual", Sección 2.10
 */
#define INCLUDE_vTaskDelay              1
/**
 * @brief Habilita la API vTaskDelete
 * @see AWS, "The FreeRTOS™ Reference Manual", Sección 2.12
 */
#define INCLUDE_vTaskDelete             1
/**
 * @brief Habilita la API xTimerPendFunctionCall usada por el port del RP2040/RP2350
 * @see AWS, "The FreeRTOS™ Reference Manual", Sección 5.13
 */
#define INCLUDE_xTimerPendFunctionCall  1
/**
 * @brief Hablita la API vTaskDelayUntil
 * @see AWS, "The FreeRTOS™ Reference Manual", Sección 2.11
 */
#define INCLUDE_vTaskDelayUntil 1
/**
 * @brief Habilita la API uxTaskPriorityGet
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 3.9
 */
#define INCLUDE_uxTaskPriorityGet 1
/**
 * @brief Habilta la API vTaskPrioritySet
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 7.3
 */
#define INCLUDE_vTaskPrioritySet  1
/**
 * @brief Habilita API vTaskResume
 * @see AWS, "The FreeRTOS™ Reference Manual", Sección 7.2
 */
#define INCLUDE_vTaskSuspend  1

#ifdef FREERTOS_USER_CONFIG
#include "FreeRTOSConfig_User.h"
#endif


#endif /* FREERTOS_CONFIG_H */