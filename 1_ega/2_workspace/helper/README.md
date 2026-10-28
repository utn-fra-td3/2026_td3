# helper

Para agregar esta biblioteca en el proyecto, incluir en el `CMakeLists.txt` general lo siguiente:

```cmake
# Añadir la subcarpeta donde está la biblioteca LCD
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../helper ${CMAKE_BINARY_DIR}/helper)
# Agrega dependencia al proyecto
target_link_libraries(firmware helper)
```

## Uso de la biblioteca

Una vez incluida la biblioteca con `#include "helper.h"`, se puede invocar en la incialización del programa para obtener distintas frecuencias de prueba a 50% de ancho de pulso de la siguiente forma:

```c
// PWM de 5KHz en GPIO 16
pwm_user_init(16, 5000);
// PWM de 10KHz en GPIO 12
pwm_user_init(12, 10000);
// PWM de 150Hz en GPIO 6
pwm_user_init(6, 150);
```