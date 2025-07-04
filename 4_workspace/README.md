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