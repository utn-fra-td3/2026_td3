# Proyecto

## EGA: analizador de respuesta en frecuencia

ESP32-WROVER-E, DDS AD9834 y detector AD8302 para ganancia y fase plegada.
Rango admitido por software: **100 kHz a 37.5 MHz**, inclusive, con MCLK
configurado en 75 MHz. No equivale a una banda de medicion RF calibrada.

- [Firmware, GUI/CLI y pruebas](2_workspace/firmware/README.md).
- [Comandos PowerShell de compilacion y carga](2_workspace/firmware/Terminal_command_cheatcode.yaml),
  con Python ESP-IDF explicito y bloques separados `hmi_build`/`hmi_flash`.
- [Esquema y frontend provisional](3_esquematico_pcb/README.md).
- [Diagramas de tareas y bloques](5_referencias/README.md).

La HMI local permite configurar un barrido SINE y ver ganancia/fase en vivo;
A cancela el barrido local y vuelve al inicio. TFT y teclado estan conectados;
el usuario confirmo mejora del teclado con escaneo independiente. AD8302
permanece desconectado: usar SIM hasta validar el frontend.

## Templates para informe IEEE

* [LaTeX en Overleaf](https://www.overleaf.com/latex/templates/ieee-conference-template/grfzhhncsfqn)
* [Google Docs](https://docs.google.com/document/d/1DDDHbyujVE0Jso3ITDVRpF-n_ydSYhf2IecZ8xTA-0w/preview?markAsViewed=false&tab=t.0)
