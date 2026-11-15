# esquemático y pcb

Subir en este directorio los archivos de esquemático y PCB en el software que se haya usado y sus vistas en .pdf.

## Esquema de conexiones actual (2026-11-14)

[Esquema modular editable, revision A](project_schematic.svg).
[Vista de impresion A3 horizontal](project_schematic.html).
Abrir la vista de impresion en un navegador y usar Imprimir / Guardar
como PDF. La exportacion automatica con Edge no produjo un archivo
verificable en esta sesion; no se incluye un PDF validado.
Los nombres de red iguales representan conexiones electricas. Los numeros
GPIO no son posiciones fisicas del conector.

Incluye ESP32-WROVER-E, AD9834, UART2, TFT KMRTM28028-SPI, MCP23017,
teclado 4x4 y filtros ADC propuestos. Los colores del cable MCP23017 y
los nombres de pads AD8302 provienen de las fotos recibidas. El dibujo
distingue la arquitectura existente de las conexiones propuestas: no es
un esquema conforme a obra ni un diseno listo para fabricar un PCB.

Siguen pendientes la alimentacion del breakout AD8302, el circuito de
alimentacion/backlight del TFT, el presupuesto de corriente y el frontend
RF. El usuario confirmo mejora del teclado HMI; detector y frontend RF
siguen sin validar. Para el AD8302 no
se debe interpretar la antigua referencia a 5 V de abajo como una
tension de alimentacion confirmada. El esquema deja VCC sin conectar.

## AD8302: frontend provisional (2026-11-07)

Objetivo vigente: DUT de 50 ohm, 100 kHz a 37.5 MHz, ganancia y fase plegada 0..180
grados. La foto del modulo no sustituye un esquema ni confirma ese rango.
No conectar siguiendo solamente el orden de pads de la foto rotada.

### Topologia inicial de prueba

```text
AD9834 Sine1 -> filtro/driver compatible con 50 ohm -> divisor resistivo
																										|          |
																						 DUT entrada     referencia
																										|          |
																						 DUT salida        |
																										|          |
																						 pad opcional  pad opcional
																										|          |
																							 INPA AD8302 INPB AD8302

VM (magnitud) -> aislamiento/filtro/proteccion -> GPIO36 ADC1_CH0
VP (fase)     -> aislamiento/filtro/proteccion -> GPIO34 ADC1_CH6
GND modulo    -> masa comun de senal/ESP32
VREF          -> sin conexion inicial; es una salida, no alimentacion
```

Esta topologia es condicional: INPA/INPB deben presentar las terminaciones
de 50 ohm previstas; si el modulo no las incorpora, redisenar o anadirlas
segun su esquema. INPA actua como carga de la salida del DUT. No anadir
otra carga de 50 ohm en paralelo sin incluirla en el presupuesto de carga.
La rama de referencia aproxima la tension de entrada del DUT solo con
ramas y cargas adaptadas; un DUT desadaptado exige muestreo en su plano
real de entrada o una correccion independiente de esa aproximacion.

Un divisor resistivo de tres puertos puede cubrir frecuencias bajas sin
transformador: tres resistencias nominales de 16.7 ohm en estrella dan
adaptacion de 50 ohm y aproximadamente 6 dB de perdida entre puertos,
cuando todos estan terminados en 50 ohm. Es una opcion de estudio, no un
PCB RF validado. Mantener cables de ramas equivalentes y layout compacto;
a decenas de MHz no usar protoboard ni largos cables Dupont para RF.
Un splitter comercial RF debe especificar funcionamiento a 100 kHz; muchos
no llegan a esa frecuencia. No conectar un tee de dos cargas de 50 ohm:
el generador veria 25 ohm.

La amplitud de 600 mVpp observada con osciloscopio de alta impedancia no
demuestra capacidad de excitar 50 ohm. Determinar impedancia y respuesta
cargada del modulo DDS antes de seleccionar buffer/filtro o atenuadores.
Si esos 600 mVpp fueran realmente sobre 50 ohm, serian unos -0.46 dBm.
El detector tiene rango operativo nominal de -60 a 0 dBm por entrada y
aproximadamente +/-30 dB de relacion. Mantener margen en ambos niveles y
en la relacion; comprobarlos con instrumentacion independiente.

### Conexion ADC y alimentacion

La asignacion GPIO34 es provisional; comprobar acceso fisico y ausencia
de otros usos. Dejar camara desconectada/deshabilitada: GPIO25/26 siguen
reservados para UART2. GPIO16/17 no estan disponibles por la PSRAM WROVER.

Como punto de partida de banco, estudiar resistencia serie de 1 kohm y
100 nF a masa junto a cada ADC: constante de tiempo de 100 us y corte de
aproximadamente 1.59 kHz. No son valores finales: verificar capacidad de
drive y estabilidad del modulo, carga del ADC y asentamiento combinado
con sus filtros. Anadir buffer/proteccion de baja fuga si lo exige el
presupuesto de error o la impedancia. No conectar RF bipolar a estos GPIO.

La marca de suministro parece indicar 5 V, pero verificar tension, polaridad,
regulador y desacoplo con vendedor/esquema antes de energizar. No alimentar
el modulo desde VREF ni desde VCC del adaptador UART. Las salidas del chip
son nominalmente de aproximadamente 0..1.8 V; comprobar las del modulo
antes de conectarlas al ESP32. No aplicar 5 V a GPIO del ESP32.

### Comprobaciones antes de cerrar esquema/BOM

- Obtener esquema o fotos nitidas de ambos lados y confirmar orden/funcion
	de pads, conexiones MSET/PSET y suministro del modulo.
- Verificar terminacion RF, capacitores de entrada, compensacion a baja
	frecuencia y filtros de salidas. Determinan si 100 kHz es alcanzable.
- Definir carga de fuente, divisor, DUT y detector, perdidas/atenuacion y
	amplitud cargada de ambas ramas en toda la banda.
- Revisar pureza del DDS y filtro cerca de 37.5 MHz; Nyquist no garantiza
	una senoide util para medicion.
- Seleccionar conectores/coax de 50 ohm, resistencias del divisor y pads
	solo despues de resolver impedancias. No hay BOM definitiva todavia.

### Secuencia de puesta en marcha

1. Cablear sin alimentacion; revisar masas, polaridad y cortocircuitos.
2. Verificar ADC con DC segura y DMM, sin modulo RF conectado.
3. Energizar modulo con fuente limitada en corriente y tension verificada;
	 comprobar VM/VP/VREF antes de conectarlos a los ADC.
4. Aplicar dos senales del mismo generador con carga y atenuacion conocidas;
	 empezar con margen, por ejemplo alrededor de -20 dBm en cada entrada.
5. Verificar thru y ganancia con atenuadores conocidos en ambas ramas;
	 comprobar signo, pendiente, offset y variacion con frecuencia.
6. Caracterizar fase con referencias verificadas, preferentemente puntos
	 interiores como 45/135 grados. Los extremos 0/180 son menos fiables.
7. Medir un DUT conocido y documentar error y rango realmente validado.

VM/VP no permiten verificar de forma fiable niveles absolutos ni ausencia
de senal. La fase plegada no distingue adelanto/atraso y una resta del thru
no recupera esa informacion. No se trata de un VNA completo ni mide S11.