# Freq Response Analyzer - ESP32 + AD9834

Este directorio contiene un proyecto base para implementar un analizador de respuesta en frecuencia usando:

- ESP32-WROVER-E
- AD9834 DDS
- AD8302: detector analogico de relacion de amplitud y fase (modulo pendiente)
- generación de barrido sinusoidal en frecuencia
- medición de amplitud/salida con ADC del ESP32
- control por software y registro de resultados

## Objetivo

Generar una señal sinusoidal con el AD9834, recorrer un rango de frecuencias
y medir la relacion entre entrada y salida del DUT para obtener curvas de
ganancia y fase plegada con el AD8302. El camino ADC de un canal se conserva
para pruebas previas y compatibilidad.

## Arquitectura propuesta

1. El ESP32 configura el AD9834 por SPI.
2. El DDS produce una onda sinusoidal con frecuencia variable.
3. El ESP32 cambia la frecuencia según un barrido configurado.
4. Se espera el asentamiento del DUT y del detector antes de leer el ADC.
5. Se adquieren las salidas de magnitud y fase del AD8302 por ADC1.
6. Se registran frecuencia, cuentas, mV y flags, y se grafican ganancia/fase.

## Estructura del firmware

- `main/main.c`: flujo principal del sistema.
- `main/ad9834_driver.c/h`: driver del AD9834.
- `main/frequency_sweep.c/h`: lógica del barrido y medición.
- `main/ad8302_measurement.c/h`: ADC1 persistente, lectura de magnitud/fase
	y simulacion explicita del detector.

## Configuración recomendada

### Pines de ejemplo

| Señal | GPIO ESP32 |
|------|------------|
| SPI MISO | No conectado (solo escritura) |
| SPI MOSI | GPIO 23 |
| SPI SCK  | GPIO 18 |
| CS       | GPIO 5 |
| RESET    | GPIO 4 |
| ADC      | GPIO 36 (ADC1_CH0) |

> Ajustar estos pines según el esquema final del hardware y la placa utilizada.

## Flujo de trabajo sugerido

1. Validar la conexión SPI con el AD9834.
2. Programar la frecuencia base y verificar la salida con osciloscopio.
3. Añadir el ciclo de barrido por frecuencias.
4. Medir la amplitud en cada punto.
5. Guardar los datos y graficarlos.
6. Compararlos con la respuesta esperada del sistema.

## Tareas pendientes

- [ ] Definir el rango operativo del barrido para el DUT; el maximo del DDS es 37.5 MHz con MCLK de 75 MHz.
- [ ] Definir la resolución en Hz por paso.
- [x] Seleccionar detector AD8302 con dos ADC para ganancia y fase plegada.
- [ ] Diseñar el circuito de acondicionamiento y atenuación.
- [ ] Documentar la topología de prueba del DUT.
- [ ] Registrar y graficar resultados.

## Referencias útiles

- AD9834 datasheet
- ESP32-WROVER-E datasheet
- ESP-IDF SPI driver documentation
- TRM del ESP32

## Local HMI: ILI9341 + MCP23017 + 4x4 keypad

The optional HMI provides SINE sweep setup and live magnitude/phase plots.
The user reports the TFT, expander and keypad connected; the camera and
AD8302 remain disconnected. Wiring/controller assumptions below still need
bench validation. This HMI firmware has not been flashed or physically tested.

| Signal | Connection |
|--------|------------|
| TFT SCK | ESP32 GPIO14, SPI2 |
| TFT MOSI / SDI | ESP32 GPIO13, SPI2 |
| TFT CS | ESP32 GPIO27 |
| TFT D/C | ESP32 GPIO32 |
| TFT RESET | ESP32 GPIO33, dedicated reset, not EN |
| MCP23017 SDA | ESP32 GPIO21 |
| MCP23017 SCL | ESP32 GPIO22 |
| Keypad rows R1..R4 | MCP23017 GPB0..GPB3 |
| Keypad columns C1..C4 | MCP23017 GPA0..GPA3 |
| Grounds | Common ESP32, TFT and MCP23017 ground |

Use 3.3 V power/logic for the MCP23017 and SDA/SCL pullups to 3.3 V.
Check the breakout's existing pullups before adding more. Verify the TFT's
VCC and backlight supply requirements from the delivered module; all TFT
logic signals must remain 3.3 V. Do not power its backlight directly from a
GPIO. TFT MISO, touch, SD card and expander interrupt pins are unused.
Verify the expander RESET is held high, not floating, and power-cycle it
before the first test (register addressing assumes BANK=0). Keypad connector
order is not standardized: identify its rows/columns with a continuity test.

Waveshare address jumpers open = high; all open means 0x27, all shorted 0x20.
The address is configurable under `EGA local HMI` in `menuconfig`.
GPIO4/5/18/23 (DDS), GPIO25/26 (UART), GPIO34/36 (ADC), and GPIO16/17
(PSRAM) remain unchanged. TFT SPI2 is independent of the DDS SPI3 bus.

### Keypad Controls

The expected physical key layout is `123A / 456B / 789C / *0#D`.

| Key | Action |
|-----|--------|
| 0..9 | Home: enter a replacement numeric value |
| * | Home: backspace |
| # | Home: accept the value into the local draft only |
| A | Return home and cancel an active local sweep; clear unfinished entry |
| B | Home: accept entry and select the next field |
| C | Home: toggle SIM/ADC source |
| D | Home: validate settings, select SINE and start the sweep |

Sweep fields are start Hz, stop Hz, points (1..256) and total ms. Both
endpoints are included, with linearly spaced integer-Hz samples. One point
requires equal endpoints. Total time must allow at least 10 ms/point at the
current 100 Hz RTOS tick and cannot exceed one hour. Saved sweep settings
seed the home draft at startup, capped to 256 points; the source deliberately
defaults to SIM on every boot while the AD8302/frontend is not connected.
Editing values or changing source does not affect hardware until D.

Local starts reserve the same BUSY gate as UART operations. Selecting SINE
and executing the sweep happen within one analyzer request, without an
intervening UART command. SIM uses the existing synthetic 100 kHz low-pass
response: SINE is selected, but the DDS frequency is not swept in SIM mode.
ADC sweeps program the DDS and acquire the detector after settling.

During a sweep, both plots update about every 100 ms. Their shared horizontal
axis is logarithmic frequency in Hz (k/M suffixes); gain is -30..+30 dB and
phase is folded magnitude 0..180 degrees, not signed lead/lag. ADC values use
nominal detector coefficients (900 mV at 0 dB, 30 mV/dB, 10 mV/degree) and
are explicitly labelled NOM, not detector-calibrated. Without ADC voltage
calibration, raw counts replace the readout and no gain/phase curve is
fabricated. Suspect input ranges show RANGE!; completed/error plots remain
visible until A. The local sample history retains every point, up to 256,
so slower display refresh does not lose intermediate samples. UART sweeps
retain their existing 10000-point limit and protocol.

A hides the plots immediately after the debounced key is accepted and
requests cancellation without entering the command queue. Local sweep waits
check cancellation in chunks of at most 20 ms; actual response also includes
key scanning, task scheduling and any acquisition/NVS work already underway.
The home screen remains BUSY until the analyzer acknowledges cancellation
(`OK:SWEEP_CANCELLED` locally). A does not cancel UART-origin operations.
Stopping a sweep leaves the DDS at its last setting; it does not disable
its output. Local samples/results do not enter PC UART transactions.

The HMI task runs at priority 2, below the analyzer and UART tasks. LVGL owns
the screen and uses a DMA-capable partial framebuffer. Key scanning requires
three matching scans, accepts one key only, and requires release before the
next key. Inactive rows remain inputs to avoid row-output contention.
I2C transfers have 20 ms timeouts; faults suppress input and retry slowly,
restoring keypad configuration and requiring release after recovery.
An initialization failure is logged and leaves the UART analyzer available.

### HMI Build And Bench Validation

Pinned managed components: `espressif/esp_lcd_ili9341` 2.0.1 and `lvgl/lvgl`
9.2.2. The component manager downloads them during configuration; generated
sources are ignored and `dependencies.lock` records the resolved versions.
HMI remains disabled by default; the normal image preserves UART-only use.
Use `hmi_build` in the command notes for a separate enabled image, or enable
`CONFIG_EGA_HMI` with `menuconfig` in the normal build after verifying wiring.
The separate enabled image retains the existing ESP32/2 MB flash settings.

Pending hardware checks: display identification/colors/text bounds, all 16
keys and connector order, held/bouncing/multiple keys, invalid numbers and
sweeps, I2C faults/recovery, NVS reboot restoration, and UART activity during
a local sweep. Verify sweep timing with display updates active. Test SIM
first: start 100000 Hz, stop 37500000 Hz, 100 points, total 10000 ms. Confirm
incremental curves, then A during a long dwell, immediately after D, and at
completion. Confirm home appears, BUSY eventually clears, and a fresh run
resets the curves. A must not cancel a PC sweep; a BUSY rejection must not
erase the current sample history. Test the one-point/equal-endpoint case and
the 256-point limit. Only then test ADC with the verified analog frontend.
Touch and SD logging remain out of scope. Compile checks and the existing
host regression tests do not validate physical TFT/keypad behaviour or
real-time cancellation latency.

## Compilación

### Windows: entorno local

El checkout copiado en `C:\ESP32\esp-idf` identifica la revision
`v6.1-dev-8199-g4d59230ddff`; su configuracion interna indica 6.2.0.
Copiar el checkout no instala el compilador ni el entorno Python local.
Los comandos de esta PC usan Python 3.11 en `C:\Espressif\tools\python`.

Instalacion inicial de herramientas (una vez):

```powershell
$env:Path = "C:\Espressif\tools\python;$env:Path"
$env:IDF_TOOLS_PATH = 'C:\Espressif'
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
& C:\ESP32\esp-idf\install.ps1 esp32
```

En cada terminal de firmware:

```powershell
$env:Path = "C:\Espressif\tools\python;$env:Path"
$env:IDF_TOOLS_PATH = 'C:\Espressif'
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
. C:\ESP32\esp-idf\export.ps1
Set-Location 'C:\CV_Tools\Automation\2026_td3\1_ega\2_workspace\firmware'
idf.py --version
idf.py -B build_local build
```

No reutilizar `build/`: conserva rutas de la PC anterior. El target ya es
`esp32`; no ejecutar `set-target` para una migracion normal, pues puede
regenerar la configuracion. Conservar `sdkconfig`.

En la extension ESP-IDF de VS Code, seleccionar esta instalacion existente,
el directorio de herramientas `C:\Espressif` y el entorno Python creado en
`C:\Espressif\python_env\idf6.2_py3.11_env`. Verificar la revision desde su
terminal antes de compilar. El interprete `.venv` de la GUI no reemplaza al
Python de ESP-IDF.

## Carga en la placa

COM4 usa un puente USB-SERIAL CH340. Esptool identifica un ESP32-D0WD-V3,
revision v3.1, con cristal de 40 MHz y flash fisica de 4 MB. Se conserva la
configuracion de 2 MB porque la imagen y las particiones caben en ella.
Cerrar cualquier programa que use COM4.

```powershell
idf.py -B build_local -p COM4 flash
idf.py -B build_local -p COM4 monitor
```

Salir del monitor con `Ctrl+]` antes de volver a cargar firmware. Si no conecta,
revisar cable USB de datos y puerto; mantener BOOT mientras conecta y soltar
al comenzar la transferencia. No es necesario borrar toda la memoria flash.

## Adaptador USB a TTL (UART2)

USB-C queda reservado para alimentacion, carga de firmware y logs (UART0).
Los comandos y sus respuestas usan UART2 a 115200 bauds, 8N1, sin control
de flujo. No se procesan comandos por USB-C con esta version.

Para el Waveshare USB TO TTL (D), colocar el selector en **3.3 V** antes
de conectar. Alimentar el ESP32 por USB-C y conectar solo estas tres lineas:

| Cable del adaptador | Pin ESP32 |
|---------------------|-----------|
| Verde TXD | GPIO26 (RX UART2) |
| Azul RXD | GPIO25 (TX UART2) |
| Negro GND | GND |
| Rojo VCC | No conectar; aislar |

Realizar el cableado con ambos equipos desconectados de la alimentacion.
No aplicar 5 V a los GPIO ni conectar el adaptador a GPIO1/3 (TX/RX del
puente USB-C). Dejar el conector SH1.0 del adaptador sin conectar.
GPIO25/26 comparten senales con la camara de esta placa Freenove:
desconectar la camara sin alimentacion y no habilitarla en este firmware.
GPIO16/17 no son alternativas apropiadas en WROVER por la PSRAM.

Conectar el USB del adaptador al equipo que ejecuta la GUI. Identificar su
puerto en el Administrador de dispositivos; COM5 es solo un ejemplo y es
distinto del COM4 del puente USB-C. En Raspberry Pi, identificar el dispositivo
en `/dev/serial/by-id/` o `/dev/ttyUSB*`.

Validacion fisica pendiente: primero probar el adaptador separado del ESP32
uniendo verde TXD con azul RXD y comprobando eco en un terminal a 115200 8N1.
Retirar ese puente antes de conectar al ESP32. Tras cargar el firmware,
comprobar `PING`, `STATUS`, `SETF 2000`, `SETW TRI`, `SETW SINE`, `ADC` y
un comando invalido (`ERR:unknown command`). Un barrido corto
`SWEEP 1000 3000 1000 50` debe entregar tres lineas
`f = <Hz> Hz | ADC = <raw>` y luego `OK:SWEEP_DONE` por UART2.
Los logs del barrido siguen disponibles por USB-C.

## Arquitectura concurrente (2026-11-08)

`uart_console_init()` crea tres tareas y verifica la reserva de recursos:

| Tarea | Prioridad | Responsabilidades |
|-------|-----------|-------------------|
| `uart_rx` | 5 | Consumir eventos de UART, ensamblar comandos y encolar operaciones |
| `uart_tx` | 4 | Transmitir respuestas y tramas completas sin mezclar bytes |
| `analyzer` | 3 | Configurar DDS, ejecutar adquisiciones/barridos y persistir ajustes |

El ISR real del driver UART envia eventos a una cola de 16 elementos.
`uart_rx` espera mediante `xQueueReceive`, sin sondeo periodico. Una cola
de comandos de un elemento comunica el receptor con `analyzer`; una cola
de 16 respuestas comunica los productores con `uart_tx`. Las colas copian
las lineas, sin conservar punteros a buffers temporales. Una seccion
critica protege las instantaneas de frecuencia, modo y estado.

Durante una operacion se aceptan `PING`, `STATUS` y `HELP`. `STATUS`
informa `STATUS=BUSY` o `STATUS=READY`. Otra configuracion, adquisicion o
barrido devuelve `ERR:BUSY`: no se modifica el DDS desde el receptor.
Las tramas de finalizacion se publican despues de liberar el estado ocupado.
La GUI mantiene una sola transaccion; estas consultas concurrentes sirven
para clientes que gestionen correctamente respuestas intercaladas.

Un comando admite hasta 255 bytes antes del terminador. Una linea demasiado
larga o con NUL se rechaza entera hasta el siguiente CR/LF. Ante overflow,
error de paridad o framing se vacia RX y se informa un fallo; enviar un
terminador y repetir el comando. Un fallo de creacion de tareas/colas se
propaga al arranque y deshabilita la salida DDS.

Validacion de hardware pendiente: cargar esta version, comprobar los tres
nombres/prioridades en el log y enviar `PING`/`STATUS` durante
`MSWEEP SIM 1000 3000 1000 1000`. Deben llegar tres muestras y
`OK:MSWEEP_DONE`; `SETF 2000` durante el barrido debe dar `ERR:BUSY`.
La HMI local y la validacion fisica del frontend siguen pendientes.

## Barrido por cantidad de puntos y tiempo

```text
MSWEEP_POINTS SIM 50000 200003 4 201
MSWEEP_POINTS ADC 50000 200003 4 201
```

Formato: `MSWEEP_POINTS <SIM|ADC> <inicio_Hz> <fin_Hz> <puntos> <total_ms>`.
La respuesta conserva `DATA:V1` y termina con `OK:MSWEEP_POINTS_DONE`.
Los puntos son lineales, incluyen ambos extremos y usan aritmetica entera:
el ejemplo genera 50000, 100001, 150002 y 200003 Hz. Se permiten 1..10000
puntos distintos; un solo punto exige inicio igual a fin. La cantidad no
puede superar `fin - inicio + 1` con resolucion de 1 Hz.

El tiempo solicitado es el presupuesto de adquisicion del barrido, no el
tiempo de escritura NVS ni el de la ultima trama en el adaptador. Se limita
a una hora y exige al menos un tick por punto: con `CONFIG_FREERTOS_HZ=100`,
`total_ms >= puntos * 10`. Se redondea el presupuesto hacia arriba a ticks
y se distribuyen plazos por punto; 201 ms se convierten en 210 ms.
El firmware cuenta configuracion DDS, adquisicion y encolado entre plazos.
Si se excede un plazo, aborta con `ERR:SWEEP failed: ESP_ERR_TIMEOUT`;
la comprobacion final admite un tick de tolerancia. La salida final por UART
puede llegar despues del presupuesto. No se omite la espera para recuperar
tiempo perdido. Elegir margen suficiente para el DUT, ADC y transporte:
el minimo permitido no garantiza un barrido util.

Los comandos anteriores `SWEEP`/`MSWEEP` conservan incremento en Hz y dwell
por punto; si el incremento no divide el rango, el ultimo punto sigue
siendo menor que el limite final. La GUI conserva ambos modos. El parser
de PC y el simulador aplican el tick actual de 10 ms; actualizar ese limite
si se cambia la configuracion FreeRTOS del proyecto.

## Persistencia NVS

Se guarda un registro versionado en namespace `ega_analyzer`, clave `config`:
frecuencia, forma de onda, rango, incremento/dwell o puntos/tiempo y fuente.
Se escribe al cambiar frecuencia/forma de onda, al aceptar configuracion
de barrido y al terminar un barrido real. No se escribe en cada punto.
Un fallo de persistencia devuelve `ERR:`; una configuracion DDS ya aplicada
puede seguir activa aunque no se haya guardado. No se informa exito falso.

Al iniciar se valida version, longitud y rangos antes de restaurar. Un
registro ausente/invalido usa seno de 1 kHz y parametros de barrido por
defecto. No se reanuda un barrido interrumpido. La salida DDS se habilita
despues de aplicar el estado restaurado. NVS no se borra automaticamente
si no puede inicializarse; consultar el log para diagnosticar el fallo.

`STATUS` conserva `FREQ`/`MODE` y agrega `STORAGE=READY|FAULT`, `RESTORED=0|1`,
`START`, `STOP`, `STEP`, `DWELL_MS`, `POINTS`, `TOTAL_MS` y `SOURCE`.
La GUI carga esos parametros desde el dispositivo al conectar o consultar
STATUS. No son ajustes guardados en la PC. `RESTORED` describe el arranque,
no indica si se guardaron cambios posteriores.

Validacion en placa pendiente: ejecutar `SETF 2000`, `SETW TRI` y consultar
STATUS; reiniciar y verificar 2000 Hz/TRI y `RESTORED=1`. Volver a SINE,
ejecutar un `MSWEEP_POINTS SIM`, reiniciar y comprobar rango/puntos/tiempo
en STATUS. Verificar por separado el fallo de almacenamiento y que no se
inicie automaticamente un barrido. Estas pruebas no validan precision RF.

## GUI UART

Crear un entorno separado desde el directorio del firmware:

```powershell
& C:\Espressif\tools\python\python.exe -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe uart_terminal.py --port COM5 --baud 115200 --gui
```

Sustituir COM5 por el puerto real del adaptador. No se necesitan cambios en
`uart_terminal.py` para este transporte. La GUI conecta automaticamente.
Solo un programa puede usar el puerto del adaptador a la vez. El monitor
USB-C en COM4 puede permanecer abierto mientras la GUI usa el adaptador;
cerrar el monitor antes de cargar firmware. Un reinicio o una carga interrumpe
cualquier comando o barrido en curso.
Seleccionar `.venv\Scripts\python.exe` en VS Code para editar la GUI.
`pyserial` es obligatorio; `matplotlib` habilita las graficas y Tkinter debe
estar incluido en Python. Los comandos tambien estan en
`Terminal_command_cheatcode.yaml` como bloques PowerShell dentro de YAML.

Validacion inicial: `PING` debe responder `OK:PONG`; `STATUS` muestra
frecuencia y modo; probar `SETF 1000` y `SETW SINE`.

El lector usa una unica transaccion hasta la respuesta de finalizacion o
error, con limites de tiempo total e inactividad calculados desde el barrido.
Los puntos iniciales ya no se descartan y no existe el corte fijo de ocho
segundos. La GUI procesa la transaccion fuera del hilo de Tk y bloquea
comandos concurrentes mientras dura la adquisicion.

Las curvas se redibujan durante la adquisicion, una vez por lote procesado
en el ciclo de Tk de 50 ms. Cada ciclo consume como maximo 128 eventos para
permitir que la interfaz atienda otros eventos. El CSV sigue requiriendo
una transaccion completa y conserva fuente, flags y calibracion nominal.

Pruebas locales sin abrir puertos ni ventanas:

```powershell
.\.venv\Scripts\python.exe -m unittest test_uart_measurement -v
```

## AD8302: desarrollo previo a la llegada del modulo

Estado (2026-11-07): implementacion de software, sin carga de esta version
en el ESP32 ni validacion fisica del AD8302. El objetivo es un DUT de 50 ohm,
desde 1 kHz hasta 37.5 MHz, con fase plegada de 0 a 180 grados. Ese rango
es un objetivo: los capacitores del modulo pueden limitar el extremo bajo
y la calidad del DDS cerca de Nyquist puede limitar el extremo alto.

La foto muestra entradas INPA/INPB, salidas de magnitud/fase y VREF, y
conexiones de alimentacion. No confirma terminacion de 50 ohm, valores de
capacitores, tension/polaridad de alimentacion ni orden exacto de pads.
Ver el [plan de acondicionamiento](../../3_esquematico_pcb/README.md).

### Adquisicion y comandos

El servicio configura ADC1 una sola vez: GPIO36/ADC1_CH0 para magnitud y
GPIO34/ADC1_CH6 para fase. GPIO34 es una asignacion provisional pendiente
de verificar en los conectores de la placa. Cada medicion real promedia
32 pares secuenciales y devuelve amplitud de variacion en cuentas ADC.
Estas lecturas son de las salidas continuas del detector, no de las ondas RF.

La conversion a mV usa calibracion line-fitting de ESP-IDF con datos eFuse.
Si no esta disponible, se conservan las cuentas y los mV valen -1; no se
aplica una formula del tipo `raw * 3.3 / 4095`. Esta calibracion del ADC
no calibra el AD8302 ni los cables o atenuadores.

Los comandos existentes `PING`, `STATUS`, `SETF`, `SETW`, `ADC` y `SWEEP`
conservan su formato de respuesta. `SWEEP` ahora espera antes de leer y
aborta ante error ADC, sin emitir un cero como punto valido. El dwell debe
ser de 1 a 60000 ms y se redondea hacia arriba al tick del RTOS.
El driver DDS existente tiene API de frecuencia sin retorno de error;
una respuesta UART no constituye verificacion de una transferencia SPI.

Nuevos comandos (fuente obligatoria y explicita):

```text
MEAS SIM
MEAS ADC
MSWEEP SIM 1000 200000 1000 50
MSWEEP ADC 1000 200000 1000 50
MSWEEP_POINTS SIM 50000 200003 4 201
```

`SIM` genera un DUT pasa-bajos ideal de primer orden con corte de 100 kHz;
no utiliza ADC ni modifica la frecuencia fisica del DDS durante MSWEEP.
Los niveles se limitan al rango nominal del detector y se marcan los
extremos sospechosos. No simula ruido ni incertidumbre del hardware.
`ADC` requiere seno; MEAS espera 50 ms y MSWEEP usa el dwell solicitado.
Ese tiempo inicial no garantiza asentamiento de cualquier DUT o modulo:
ajustarlo segun la respuesta del circuito y los filtros del detector.

Ejemplo de punto simulado en 100 kHz, seguido de finalizacion:

```text
DATA:V1,F=100000,SEQ=0,T_US=123,SOURCE=SIM,MAG_RAW=-1,PHASE_RAW=-1,MAG_MV=810,PHASE_MV=1350,MAG_SPAN=-1,PHASE_SPAN=-1,FLAGS=9
OK:MEAS_DONE
```

`F` esta en Hz; `SEQ` empieza en cero; `T_US` marca el inicio de adquisicion
en microsegundos desde el arranque. Los campos RAW/SPAN usan cuentas;
SPAN es maximo menos minimo. En simulacion son -1 porque no hubo ADC.
MEAS termina con `OK:MEAS_DONE`; MSWEEP con `OK:MSWEEP_DONE`;
MSWEEP_POINTS con `OK:MSWEEP_POINTS_DONE`; un fallo
termina con `ERR:`. Los flags son una mascara:

| Bit / valor | Significado |
|-------------|-------------|
| 0 / 1 | Simulacion explicita |
| 1 / 2 | ADC sin calibracion; mV no disponibles |
| 2 / 4 | Extremo/saturacion sospechosa; no usar como punto fiable |
| 3 / 8 | AD8302 sin calibracion de banco |

La GUI convierte nominalmente `gain_db = (MAG_MV - 900) / 30` y
`phase_folded_deg = (1800 - PHASE_MV) / 10`. Deben verificarse polaridad,
offset y pendiente con el modulo real: estos coeficientes no son una
calibracion. Los CSV incluyen fuente, flags, cuentas, mV, frecuencia,
secuencia, tiempo y `CALIBRATION=NOMINAL_DETECTOR`.
La GUI omite puntos con flags 2/4 de las curvas, pero los conserva en CSV.
Los flags de rango no prueban presencia de RF ni niveles absolutos validos.
La fase plegada no permite distinguir adelanto de atraso, y no debe
interpretarse como fase firmada o corregirse por resta universal del thru.

### Prueba sin hardware

Desde este directorio, el mismo entorno de la GUI permite ejecutar:

```powershell
.\.venv\Scripts\python.exe uart_terminal.py --simulate --gui
.\.venv\Scripts\python.exe uart_terminal.py --simulate --command "MSWEEP SIM 10000 200000 10000 50" --csv ad8302_sim.csv
.\.venv\Scripts\python.exe -m unittest test_uart_measurement -v
```

El modo offline no abre ningun puerto serial y no espera el dwell en tiempo
real; sus timestamps son los tiempos del barrido modelado. Usar MSWEEP para
obtener ambas curvas, y Export CSV para una transaccion de medicion completa.
El preview DDS sigue siendo sintetico. Sin `--simulate`, SIM requiere la
nueva version de firmware y funciona por UART2; no confundir ambos modos.

Verificacion local: build ESP-IDF completo; pruebas de parser, conversion
nominal, flags, CSV, simulacion, secuencia y respuestas UART fragmentadas,
en lotes grandes, con timeout/desconexion y barridos de mas de ocho segundos.
Smoke test Tk offline: 20 puntos y ambas curvas renderizadas, sin puerto.
Las advertencias Pylance de dependencias preexistentes dependen del
interprete seleccionado; las pruebas usan el `.venv` local documentado.

Pendiente para medicion real: verificar modulo y cableado, probar ambos ADC
con tensiones DC conocidas/DMM, medir asentamiento y niveles RF cargados,
calibrar ganancia/fase por frecuencia y comparar un DUT conocido.

### Resultados locales (2026-10-30)

- Build en `build_local` completo tras corregir DDS: imagen de 239680 bytes.
- Carga en COM4 completada con verificacion de hash.
- UART: `PING`, `STATUS`, `SETF 1000`, `SETW SINE` y `ADC` respondieron OK.
- GUI: conexion real a COM4 y respuesta `OK:PONG` verificadas.
- Python de la GUI: 3.11.15, pyserial 3.5, matplotlib 3.11.2, Tk 8.6.12.
- YAML de comandos validado con un parser; dependencias sin conflictos.
- La salida fisica del AD9834 y la medicion calibrada no se han verificado.

## Validacion del hardware

El firmware esta configurado para un MCLK de 75 MHz. Se midieron
aproximadamente 3 kHz en Sine1 al solicitar 1 kHz con la configuracion
anterior de 25 MHz; tras cambiar el MCLK configurado, Sine1 mide 1 kHz.
La medicion confirma el ajuste para este modulo.
El rango de frecuencia aceptado por el firmware y la GUI es de 1 Hz a
37.5 MHz (MCLK/2), el maximo especificado para el AD9834.
Prueba adicional con la GUI (2026-11-01): el control de frecuencia funciona;
`SINE` y `TRI` aparecen en Sine1 y Sine2 con 180 grados de desfase, y
`SQUARE` aparece en la salida Square.
Tras cargar el firmware con limite de 37.5 MHz, el usuario confirmo que la GUI
sigue funcionando correctamente.
Confirmar el MCLK directamente si es posible, ademas de la alimentacion del
modulo, niveles logicos y tierra comun. No aplicar 5 V a
GPIO del ESP32, ni una onda bipolar directamente a GPIO36: el ADC necesita
una entrada acondicionada dentro de sus limites.

Las respuestas UART solo confirman estado de software. La vista de onda de
la GUI es sintetica y los datos ADC son cuentas sin calibracion. Verificar
la salida real con osciloscopio o contador de frecuencia.

### Correcciones del driver AD9834 (2026-10-30)

- RESET fisico activo alto: pulso alto y retorno a bajo, no dejarlo alto.
- SPI modo 2, SCLK en reposo alto y captura en flanco descendente.
- Cada palabra de 16 bits se transmite primero por su byte alto.
- Corregidos MODE (bit 1), DIV2 (bit 3), SIGN/PIB (bit 4), FSELECT (bit 11)
	y los bits SLEEP1/SLEEP12.
- Control persistente: cambiar frecuencia no borra el modo ni el estado de
	habilitacion. PIN/SW queda en cero para seleccion por software.
- Frecuencia: B28 en el registro de control, luego exactamente dos palabras
	de 14 bits con direccion FREQ0=0x4000 o FREQ1=0x8000. Se elimina una
	escritura extra incorrecta que desalineaba las mitades del acumulador.
- Fase inicial cero y frecuencia cargada antes de liberar RESET por software.

Con MCLK=75 MHz y salida de 1 kHz, FREQ0 recibe `0x4DFB`, `0x4000`;
el control final para seno es `0x2000`. Son valores esperados del software,
no una captura de las lineas fisicas. Build, carga con verificacion de hash,
`PING`, `STATUS` y `SETF 1000` se verificaron de nuevo el 2026-11-01.
La salida Sine1 se midio a 1 kHz despues de esta correccion.

Referencia de registros y formato de transferencia:
[driver de referencia Analog Devices](https://github.com/analogdevicesinc/no-OS/tree/main/drivers/frequency/ad9833).

### Primera prueba en Sine1

1. Seleccionar `SETW SINE` y `SETF 1000`, o reiniciar la placa.
2. Conectar punta a Sine1 y masa al GND del modulo. Usar entrada de 1 Mohm,
	 no terminacion de 50 ohm, salvo que el esquema del modulo la requiera.
3. Comenzar con acoplamiento DC, 200 us/div y unos 100 mV/div; ajustar
	 sensibilidad y posicion segun la amplitud y offset del modulo. Ajustar
	 el factor de sonda en el osciloscopio.
4. Verificar RESET/GPIO4 bajo tras iniciar, alimentacion y tierra comun.
	 Confirmar que MCLK realmente oscila y su frecuencia; 75 MHz se infiere
	 de la lectura de salida y todavia debe contrastarse con el oscilador del
	 modulo. Ajustar `mclk_hz` en `main/main.c` si la medicion de MCLK difiere.
5. Si sigue sin senal, observar FSYNC/GPIO5, SCLK/GPIO18 y SDATA/GPIO23 al
	 reiniciar o enviar `SETF 1000`: FSYNC bajo durante cada palabra de 16 bits,
	 reloj de 1 MHz y datos MSB primero. Revisar tambien SLEEP y las entradas
	 de seleccion del modulo segun su esquema; no dejarlas flotantes.

El AD9834 tiene salidas analogicas y una salida digital SIGN BIT OUT distinta:
`SETW SQUARE` habilita esta ultima, no convierte Sine1 en salida cuadrada.
Confirmar en el esquema del modulo como Sine1 y Sine2 se conectan a las
salidas analogicas del chip; sus etiquetas no seleccionan FREQ0/FREQ1.

## Importante

Este es un punto de partida funcional, no un firmware terminado. La lógica del barrido, la salida del DDS y la medición deben validarse con hardware real y ajustarse según el diseño final.
