# Waveshare USB TO TTL (D)

- [Hoja de datos FT232RN](DS_FT232RN.pdf), FTDI.
- Copia enlazada por Waveshare, descargada el 2026-11-14:
  https://www.waveshare.com/w/upload/c/c5/DS_FT232RN.pdf
- Guia del modulo:
  https://www.waveshare.com/wiki/USB_TO_TTL_(D)

Waveshare identifica FT232RNL en la version actual del producto;
comprobar la revision/chip del adaptador recibido. El VID/PID por si
solo no identifica la revision del silicio.
Para este proyecto seleccionar logica de 3.3 V, conectar TXD verde a
GPIO26, RXD azul a GPIO25 y GND negro a GND. Dejar VCC rojo desconectado.
La hoja del chip no sustituye el circuito de seleccion de nivel del cable.