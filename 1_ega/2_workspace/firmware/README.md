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
MEAS termina con `OK:MEAS_DONE`; MSWEEP con `OK:MSWEEP_DONE`; un fallo
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
