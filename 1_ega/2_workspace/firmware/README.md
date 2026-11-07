# Freq Response Analyzer - ESP32 + AD9834

Este directorio contiene un proyecto base para implementar un analizador de respuesta en frecuencia usando:

- ESP32-WROVER-E
- AD9834 DDS
- generación de barrido sinusoidal en frecuencia
- medición de amplitud/salida con ADC del ESP32
- control por software y registro de resultados

## Objetivo

Generar una señal sinusoidal con el AD9834, recorrer un rango de frecuencias y medir la respuesta del sistema bajo prueba para obtener una curva de amplitud frente a frecuencia.

## Arquitectura propuesta

1. El ESP32 configura el AD9834 por SPI.
2. El DDS produce una onda sinusoidal con frecuencia variable.
3. El ESP32 cambia la frecuencia según un barrido configurado.
4. Se toma una medida de amplitud con ADC o con un circuito de acondicionamiento.
5. Se guarda el par (frecuencia, amplitud) para analizarlo posteriormente.

## Estructura del firmware

- `main/main.c`: flujo principal del sistema.
- `main/ad9834_driver.c/h`: driver del AD9834.
- `main/frequency_sweep.c/h`: lógica del barrido y medición.

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
- [ ] Definir si la medición es por ADC, RMS o pico a pico.
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

Salir del monitor con `Ctrl+]` antes de iniciar la GUI. Si no conecta,
revisar cable USB de datos y puerto; mantener BOOT mientras conecta y soltar
al comenzar la transferencia. No es necesario borrar toda la memoria flash.

## GUI UART

Crear un entorno separado desde el directorio del firmware:

```powershell
& C:\Espressif\tools\python\python.exe -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe uart_terminal.py --port COM4 --baud 115200 --gui
```

La GUI conecta automaticamente. Solo un programa puede usar COM4 a la vez:
cerrar la GUI antes de volver a cargar firmware o abrir el monitor.
Seleccionar `.venv\Scripts\python.exe` en VS Code para editar la GUI.
`pyserial` es obligatorio; `matplotlib` habilita las graficas y Tkinter debe
estar incluido en Python. Los comandos tambien estan en
`Terminal_command_cheatcode.yaml` como bloques PowerShell dentro de YAML.

Validacion inicial: `PING` debe responder `OK:PONG`; `STATUS` muestra
frecuencia y modo; probar `SETF 1000` y `SETW SINE`.

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
