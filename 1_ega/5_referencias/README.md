# referencias

Subir en este directorio todos lo que haya servido de referencia para la EGA: hojas de datos, bibliografía, artículos, etc.

## Diagramas actualizados (2026-11-14)

- [Diagrama de tareas y comunicaciones](task_diagram.svg): prioridades,
  stacks en bytes, colas, estado compartido y HMI opcional.
- [Diagrama en bloques del sistema](block_diagram.svg): control digital,
  adquisicion ADC y camino RF propuesto.

Los SVG son editables y se abren en un navegador o editor vectorial.
Los archivos Excalidraw anteriores se conservan sin cambios; los SVG
son las nuevas vistas basadas en el firmware actual.

Hay tres tareas de aplicacion principales: `uart_rx` (prioridad 5),
`uart_tx` (4) y `analyzer` (3). `local_hmi` (2) es opcional y esta
deshabilitada por defecto. Los barridos, la adquisicion y NVS se ejecutan
dentro de `analyzer`, no en tareas adicionales. El estado local se lee
mediante una instantanea protegida y un historial de hasta 256 muestras para
las curvas locales de ganancia y fase. No hay una cola de resultados HMI.
La solicitud de cancelar un barrido local evita la cola de comandos y se
consulta durante las esperas, en intervalos de hasta 20 ms; no cancela
operaciones iniciadas por UART. La latencia fisica sigue sin validar.

Las lineas discontinuas de hardware indican conexiones propuestas o no
validadas fisicamente. No confundir implementacion de software con
validacion del TFT, detector, alimentacion o frontend RF. Para los detalles
de conexion, consultar el [esquema modular](../3_esquematico_pcb/project_schematic.svg).