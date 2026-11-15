# Detector AD8302

- [Hoja de datos AD8302](AD8302.pdf), Analog Devices,
  LF-2.7 GHz RF/IF Gain and Phase Detector.
- Descarga: 2026-11-14, copia del distribuidor Farnell:
  https://www.farnell.com/datasheets/2287613.pdf
- Fuente original del fabricante (timeout durante esta descarga):
  https://www.analog.com/media/en/technical-documentation/data-sheets/AD8302.pdf

Es la hoja de datos del circuito integrado, no del breakout recibido.
Faltan fabricante/revision, esquema, alimentacion admisible, terminaciones
RF, acoplamiento de entrada y conexiones MSET/PSET del modulo.
El rango del chip no demuestra la banda util del modulo a 100 kHz.
Verificar VMAG/VPHS antes de conectarlos al ESP32; VREF es una salida.