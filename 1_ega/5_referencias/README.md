# referencias

Subir en este directorio todos lo que haya servido de referencia para la EGA: hojas de datos, bibliografía, artículos, etc.

## Hojas de datos por modulo (2026-11-14)

| Modulo | Documentacion local |
| --- | --- |
| DDS AD9834 | [Hoja de datos, AN-1070 y pendientes del modulo](AD9834/README.md) |
| ESP32-WROVER | [Hoja de datos](ESP32_WROVER/esp32-wrover_datasheet_en.pdf), [manual tecnico](ESP32_WROVER/esp32_technical_reference_manual_en.pdf) y [pinout](ESP32_WROVER/ESP32-Pinout_V3.0.pdf), ya existentes |
| Detector AD8302 | [Hoja de datos y pendientes del breakout](AD8302/README.md) |
| Expansor Waveshare MCP23017 | [Hoja de datos, esquema y manual del modulo](MCP23017/README.md) |
| TFT KMRTM28028-SPI | [Referencia ILI9341; controlador por confirmar](TFT_KMRTM28028_SPI/README.md) |
| Adaptador Waveshare USB TO TTL (D) | [Referencia FT232RN y guia del modulo](USB_TO_TTL_D/README.md) |
| DUT Mini-Circuits SLP-1.9+ | [Hoja de datos](SLP-1.9+/README.md) |
| DUT Mini-Circuits SLP-5+ | [Hoja de datos](SLP-5+/README.md) |

Cada carpeta nueva identifica la fuente y fecha de descarga. Los PDF se
conservan sin modificar. El AD8302 se obtuvo del distribuidor Farnell al
fallar la descarga directa de Analog Devices; el ILI9341 es una copia
alojada por Adafruit. Las hojas de datos de circuitos integrados no
sustituyen el esquema del modulo comercial ni confirman su alimentacion,
terminacion RF o pinout. Los documentos previos se conservan sin cambios.

Pendientes: esquema exacto de los modulos DDS y AD8302, documentacion del
TFT entregado y modelo/pinout del teclado 4x4. No se asigna una hoja de
datos generica al teclado sin identificar su modelo y conector.

## Diagramas actualizados (2026-11-14)

- [Diagrama de tareas y comunicaciones](task_diagram.svg): prioridades,
  stacks en bytes, colas, estado compartido y HMI opcional.
- [Diagrama en bloques del sistema](block_diagram.svg): control digital,
  adquisicion ADC y camino RF propuesto.

Los SVG son editables y se abren en un navegador o editor vectorial.
Los archivos Excalidraw anteriores se conservan sin cambios; los SVG
son las nuevas vistas basadas en el firmware actual.

Hay tres tareas de aplicacion principales: `uart_rx` (prioridad 5),
`uart_tx` (4) y `analyzer` (3). `local_hmi` (2, stack 6144 bytes) y
`hmi_keypad` (2, stack 3072 bytes) son opcionales y estan deshabilitadas
por defecto. El teclado se escanea de forma independiente, con debounce
de dos scans y una cola de 16 enteros para teclas y avisos I2C; LVGL solo
se usa desde `local_hmi`. El usuario confirmo mejora de respuesta.
Los barridos, la adquisicion y NVS se ejecutan
dentro de `analyzer`, no en tareas adicionales. El estado local se lee
mediante una instantanea protegida y un historial de hasta 256 muestras para
las curvas locales de ganancia y fase. No hay una cola de resultados HMI.
La solicitud de cancelar un barrido local evita la cola de comandos y se
consulta durante las esperas, en intervalos de hasta 20 ms; no cancela
operaciones iniciadas por UART. La latencia fisica sigue sin validar.

El rango vigente de salida y extremos de barrido es 100 kHz..37.5 MHz,
inclusive; no es una banda RF calibrada. Los incrementos pueden ser menores
que 100 kHz. La fuente SIM permite probar las curvas sin el AD8302 conectado.

Las lineas discontinuas de hardware indican conexiones propuestas o no
validadas fisicamente. No confundir implementacion de software con
validacion del TFT, detector, alimentacion o frontend RF. Para los detalles
de conexion, consultar el [esquema modular](../3_esquematico_pcb/project_schematic.svg).