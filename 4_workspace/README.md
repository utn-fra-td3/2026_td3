# workspace

Este directorio tiene un conjunto de ejemplos de los temas que se tratan en la cátedra y funciona como un espacio de prueba.

> ‼️ Este directorio no se usa como espacio para hacer entregas. No debe agregarse a git ningún directorio o proyecto que se pruebe en este espacio.

## FreeRTOS

Los ejemplos de FreeRTOS se mencionan a continuación separados por temáticas como están descritas en el libro [Mastering the FreeRTOS™ Real Time Kernel](../5_docs/unidades_tematicas/1_rtos/bibliografia/Mastering-the-FreeRTOS-Real-Time-Kernel.v1.1.0.pdf) por Richard Barry y el equipo de FreeRTOS.

### Heap Memory Management

| Ejemplo | Descripción |
| --- | --- |
| [freertos_blinky](freertos_blinky) | Ejemplo de creación de tarea sencilla de forma dinámica |
| [freertos_static_blinky](freertos_static_blinky) | Ejemplo de creación de tarea sencilla de forma estática con implementaciones de memoria estática para la IDLE Task y el Timer Service Task

### Task Management

| Ejemplo | Descripción |
| --- | --- |
| [freertos_task_handle](freertos_task_handle) | Ejemplo de manejo de prioridades de las tareas y eliminación a través del handle |
| [freertos_task_params](freertos_task_params) | Creación de tareas a partir de parámetros |

### Queue Management

| Ejemplo | Descripción |
| --- | --- |
| [freertos_queue_basic](freertos_queue_basic) | Ejemplo de implementación de una cola de tipo de dato sencillo |
| [freertos_queue_typedef](freertos_queue_typedef) | Ejemplo de colas con datos complejos como estructuras |
| [freertos_static_queue](freertos_static_queue) | Ejemplo de cola con datos complejos como estructuras pero usando recursos estáticos |

### Software Timer Management

| Ejemplo | Descripción |
| --- | --- |
| [freertos_software_timer](freertos_software_timer) | Ejemplo de un timer one shot y auto reload corriendo en el Timer Service Task |
| [freertos_static_software_timer](freertos_static_software_timer) | Ejemplo de un timer one shot y auto reload corriendo en el Timer Service Task con todos los recursos estáticos |

### Interrupt Management

| Ejemplo | Descripción |
| --- | --- |
| [freertos_semphr](freertos_sepmhr) | Ejemplo de sincronización de tareas con semáforo binario sin interrupción |
| [freertos_semphr_irq](freertos_semphr_irq) | Ejemplo de sincronización de tareas con semáforo binario desde una interrupción |
| [freertos_semphr_counting_irq](freertos_semphr_counting_irq) | Ejemplo de conteo de eventos con semáforo counting desde interrupción |
| [freertos_queue_irq](freertos_queue_irq) | Ejemplo de escritura de datos en una cola desde una interrupción |

## GPOS

Los ejemplos de Linux Embebido en Sistemas Operativos de Propósito General (GPOS) se mencionan a continuación. Están separados por temáticas y fueron compilados en la versión `6.12.25` del kernel de Linux.

Para corroborar la versión del kernel de Linux que se está usando se puede correr:

```bash
uname -r
```

### Preliminares

| Ejemplo | Descripción |
| --- | --- |
| [gpos_hello_kenel](gpos_hello_kernel/) | Ejemplo simple de módulo de kernel que envía mensajes por el servicio de mensajes del kernel al cargar y descargar el módulo |
| [gpos_param](gpos_param/) | Ejemplo que carga un módulo en el kernel con parámetros |

### Hilos

| Ejemplo | Descripción |
| --- | --- |
| [gpos_kthread_v1](gpos_kthread_v1/) | Ejemplo de dos hilos que imprimen mensajes en el kernel |
| [gpos_kthread_v2](gpos_kthread_v2/) | Ejemplo de dos hilos que imprimen mensajes en el kernel |

### Sincronización

| Ejemplo | Descripción |
| --- | --- |
| [gpos_mutex](gpos_mutex/) | Ejemplo de dos hilos que se sincronizan mediante un mutex |
| [gpos_waitqueue](gpos_waitqueue/) | Ejemplo que bloquea hilos mediante una cola |

### Archivos

| Ejemplo | Descripción |
| --- | --- |
| [gpos_char_dev](gpos_char_dev/) | Ejemplo que registra un char device |
| [gpos_char_dev](gpos_char_dev_fops/) | Ejemplo con file operations en un char device |
| [gpos_procfs](gpos_procfs/) | Ejemplo con file operations en un proc file |

### Device Tree

| Ejemplo | Descripción |
| --- | --- |
| [gpos_dev_tree_uart](gpos_dev_tree_uart/) | Ejemplo de un device tree para el UART y uso con una aplicación de usuario |

### Interacción user space y kernel space

| Ejemplo | Descripción |
| --- | --- |
| [gpos_dev_tree_uart](gpos_dev_tree_uart/) | Ejemplo que manda y recibe mensajes por UART a partir de lo que se escribe en una aplicación de usuario |