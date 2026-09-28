# lcd

Para agregar esta biblioteca en el proyecto, incluir en el `CMakeLists.txt` general lo siguiente:

```cmake
# Añadir la subcarpeta donde está la biblioteca LCD
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../lcd ${CMAKE_BINARY_DIR}/lcd)
# Agrega dependencia al proyecto
target_link_libraries(firmware lcd)
```

## Uso de la biblioteca

Una vez incluida la biblioteca con `#include "lcd.h"` podemos hacer algo básico con el LCD usando:

```c
// Inicializa el LCD con el I2C0 y la direccion de 7 bits 0x27
lcd_init(i2c0, 0x27);
// Limpia la pantalla
lcd_clear();
// Escribe un mensaje
lcd_string("Hello world!");
```

> :warning: La inicializacion del I2C de la Raspberry Pi Pico y los GPIO deben hacerse previamente.

## Ejemplos

Los ejemplos que van a encontrarse en el repositorio son:

| Ejemplo | Descripcion |
| ------- | ----------- |
| [hello_world](examples/hello_world/main.c) | Ejemplo basico que escribe un mensaje fijo en dos lineas del LCD |
| [sprintf](examples/sprintf/main.c) | Ejemplo que usa el sprintf de la biblioteca de stdio.h para imprimir una variable en el LCD |
