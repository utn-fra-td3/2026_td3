# Encoder rotativo

Biblioteca general para encoders incrementales con pulsador y resistencias pull-up internas de Raspberry Pi Pico.
No depende de FreeRTOS: el programa solamente entrega el tiempo actual en milisegundos.

## Agregarla a un proyecto

```cmake
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../encoder ${CMAKE_BINARY_DIR}/encoder)
target_link_libraries(mi_programa encoder)
```

## Uso

```c
#include "encoder.h"

encoder_t encoder;
encoder_init(&encoder, 2, 3, 4, 30, 800, to_ms_since_boot(get_absolute_time()));

int movement = encoder_poll_rotation(&encoder);
encoder_button_event_t button = encoder_poll_button(&encoder, to_ms_since_boot(get_absolute_time()));
```

`encoder_poll_rotation` devuelve `-1`, `0` o `1`. El sentido final puede invertirse multiplicando el resultado por `-1`.

`encoder_poll_button` devuelve:

- `ENCODER_BUTTON_EVENT_NONE`
- `ENCODER_BUTTON_EVENT_SHORT`
- `ENCODER_BUTTON_EVENT_LONG`

## Ejemplos

| Ejemplo | Descripcion |
| ------- | ----------- |
| `examples/read_encoder/main.c` | Muestra giros, pulsacion corta y pulsacion larga por USB serie |
