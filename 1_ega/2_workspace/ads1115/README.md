# ADS1115

Biblioteca general para usar el conversor ADS1115 con Raspberry Pi Pico y Pico SDK.
No inicializa el periferico I2C ni los GPIO: cada proyecto decide que bus, pines y frecuencia utiliza.

## Agregarla a un proyecto

```cmake
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../ads1115 ${CMAKE_BINARY_DIR}/ads1115)
target_link_libraries(mi_programa ads1115)
```

## Uso sencillo

```c
#include "ads1115.h"

ads1115_t ads;
ads1115_init(&ads, i2c0, ADS1115_ADDRESS_GND, ADS1115_GAIN_4_096V, ADS1115_DATA_RATE_860_SPS);

uint16_t raw;
if (ads1115_read_single_ended_blocking(&ads, 0, &raw)) {
    float pin_voltage = ads1115_raw_to_voltage(&ads, (int16_t)raw);
    float input_voltage = ads1115_restore_divider_voltage(pin_voltage, 330000.0f, 22000.0f);
}
```

## Funciones principales

- `ads1115_init`: guarda el I2C, direccion, ganancia y velocidad.
- `ads1115_read_single_ended_blocking`: lee AIN0, AIN1, AIN2 o AIN3 de forma sencilla.
- `ads1115_start_single_ended` y `ads1115_read_conversion`: permiten esperar con FreeRTOS sin bloquear la tarea.
- `ads1115_raw_to_voltage`: convierte las cuentas del ADS en tension del pin.
- `ads1115_restore_divider_voltage`: recupera la tension anterior a un divisor resistivo.

Si el ADS comparte el bus con otros dispositivos, el programa debe proteger cada operacion I2C con su propio mutex.

## Ejemplos

| Ejemplo | Descripcion |
| ------- | ----------- |
| `examples/read_channel/main.c` | Lee AIN0 y recupera una tension medida mediante divisor resistivo |
