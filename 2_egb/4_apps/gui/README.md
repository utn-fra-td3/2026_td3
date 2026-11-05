# Panel web EGA en la Zero

`ega_web.py` sirve la página en `GET /` y recibe órdenes en `POST /api/command` con JSON `{"command": "..."}`. El servidor validado en la Zero utiliza el mismo archivo Python que este directorio. La comprobación del servicio y del hash del archivo no demuestra el estado de las salidas ni la versión de la CLI instalada.

## Protocolo

La página usa nombres `P` y `N`. `translate_command` los convierte en índices `0` y `1` antes de escribir en `/dev/ega_uart`: `SET P 12.5` se envía como `V 0 12.5`, `OUT N OFF` como `OUT 1 OFF` y `RESET P` como `RST 0`. `GET` y `SAVE` conservan sus nombres. Una respuesta `OK QUEUED` confirma el encolado en la Pico; no confirma la tensión alcanzada ni que `SAVE` terminó de escribir Flash. Los avisos `PROT` se separan de la respuesta y se exponen en el campo JSON `notifications`.

La interfaz y `validate_command` aún aceptan consignas de 0 a 30 V, mientras el firmware Pico de esta entrega acepta `V` desde 5 V. Entre 0 y menos de 5 V, el servidor envía la orden traducida y devuelve el error `ERR RANGE` de la Pico. Este desajuste sigue pendiente.

## Actualización de la página

La página intenta `GET` cada segundo. Después de un comando correcto hace otro `GET` para mostrar telemetría reciente. Cada clic en `+` o `−` de un límite envía una orden inmediatamente; esta versión no agrupa clics ni aplica una espera de 350 ms. La variable `busy` evita el refresco periódico mientras la página marca otra operación en curso; el servidor Python serializa transacciones UART con un lock.

## Pruebas locales

Desde este directorio: `python3 -B -m unittest -v test_ega_web`. Las pruebas de traducción y selección de respuestas usan un transporte simulado: no abren el dispositivo ni prueban hardware. El archivo `Guia_de_usuario.txt` describe el uso y los límites observables.
