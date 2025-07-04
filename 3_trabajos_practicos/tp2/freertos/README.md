# freertos

Este directorio contiene la biblioteca de FreeRTOS (v202210.01 LTS) que vamos a usar en la cátedra.

> :warning: **No se debe modificar ningún archivo de este directorio**

## Incluir FreeRTOS

Para agregar esta biblioteca en el proyecto, incluir al final del `CMakeLists.txt` del proyecto lo siguiente:

```cmake
# Configuración adicional de FreeRTOS de la aplicación (si el archivo existe)
set(FREERTOS_USER_CONFIG "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "User FreeRTOSConfig path")
# Habilito uso de memoria dinámica
set(FREERTOS_SUPPORT_DYNAMIC_ALLOCATION "ON" CACHE PATH "FreeRTOS dynamic memory implementation support")
# Añadir la subcarpeta donde está la biblioteca FreeRTOS
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../freertos ${CMAKE_BINARY_DIR}/freertos)
# Agrega dependencia al proyecto
target_link_libraries(${PROJECT_NAME} freertos)
```

## Configuración de FreeRTOS personalizada

Muchas de las particularidades de FreeRTOS se configuran desde el archivo [FreeRTOSConfig.h](FreeRTOSConfig.h). El que se provee con la biblioteca va a servir para la mayoría de los casos. Sin embargo, si una determinada aplicación requiriera de alguna configuración particular (más memoria dinámica, acceso a alguna API en especial, memoria estática, etc), es posible pisar o agregar a esa configuración creando un `FreeRTOSConfig_User.h` en el propio directorio del proyecto. 

Este archivo debe estar en el raíz del proyecto de esta forma:

```
firmware
├── .vscode/
├── pico_sdk_import.cmake
├── firmware.c
├── CMakeLists.txt
└── FreeRTOSConfig_User.h
```

En caso de que no exista, el proyecto simplemente hace uso de la configuración provista en la biblioteca. Algunos ejemplos de este directorio incluyen configuración especial para tener de referencia.

> :warning: Ya sea que se pase de usar el FreeRTOSConfig_User.h a no usarlo o viceversa, se recomienda hacer una limpieza de los archivos de CMake a través del comando _Clean CMake_ de la extensión de Raspberry Pi Pico antes de compilar para que los cambios tomen efecto en la compilación.

### Referencias de FreeRTOSConfig.h

A continuación dejamos algunas lecturas recomendadas para tomar referencias de las configuraciones y símbolos posibles:

 - ["Mastering the FreeRTOS Real Time Kernel"](https://github.com/FreeRTOS/FreeRTOS-Kernel-Book/releases/download/V1.1.0/Mastering-the-FreeRTOS-Real-Time-Kernel.v1.1.0.pdf) por Richard Barry y el equipo de FreeRTOS.
 - ["The FreeRTOS™ Reference Manual"](https://www.freertos.org/media/2018/FreeRTOS_Reference_Manual_V10.0.0.pdf) por Amazon Web Services, particularmente el capítulo 7.
 - [Customization](https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/02-Customization) por Amazon Web Services.
 - [Using FreeRTOS on ARMv8-M Microcontrollers](https://www.freertos.org/Community/Blogs/2020/using-freertos-on-armv8-m-microcontrollers) por Gaurav Aggarwal.

## Implementación de Heap

La biblioteca usa por defecto la implementación 4 de heap. Si por alguna razón se requiriera una implementación distinta, basta con agregar lo siguiente al `CMakeLists.txt` del proyecto:

```cmake
# Defino implementación de heap (ejemplo con heap 3)
set(FREERTOS_HEAP "heap_3" CACHE STRING "FreeRTOS heap implementation")
```

> ⚙️ Ya sea que se pase de usar el la implementación de heap de la biblioteca al propio o viceversa, se recomienda hacer una limpieza de los archivos de CMake a través del comando _Clean CMake_ de la extensión de Raspberry Pi Pico antes de compilar compilar.

## Acerca de uso de memoria estática

FreeRTOS permite hacer uso de memoria dinámica, estática o ambas. Si en alguna aplicación se deseara hacer uso exclusivamente de la memoria estática, es necesario hacer algunos ajustes.

En primer lugar, hay que crear una configuración de usuario de `FreeRTOSConfig_User.h` que contenga lo siguiente:

```c
/**
 * @brief Soporte para memoria estática sin uso de memoria dinámica
 * @see Barry, R. "Mastering the FreeRTOS Real Time Kernel", Sección 3.4
 */
#define configSUPPORT_STATIC_ALLOCATION 1
#define configSUPPORT_DYNAMIC_ALLOCATION  0
```

Esto anula las implementaciones dinámicas de las APIs de FreeRTOS y nos deja solo con las estáticas. Aún así, FreeRTOS va a seguir linkeando la implementación de heap de la biblioteca. Para anularla, es necesario editar una línea de nuestro `CMakeLists.txt` del proyecto cambiando:

```cmake
# Habilito uso de memoria dinámica
set(FREERTOS_SUPPORT_DYNAMIC_ALLOCATION "ON" CACHE PATH "FreeRTOS dynamic memory implementation support")
```

Por esto:

```cmake
# Deshabilito el uso de memoria dinámica
set(FREERTOS_SUPPORT_DYNAMIC_ALLOCATION "OFF" CACHE PATH "FreeRTOS dynamic memory implementation support")
```