# Waveshare MCP23017 IO Expansion Board

Documentos descargados el 2026-11-14:

| Documento | Archivo local | Fuente |
| --- | --- | --- |
| Microchip MCP23017/MCP23S17 | [Hoja de datos](MCP23017.pdf) | https://ww1.microchip.com/downloads/en/DeviceDoc/20001952C.pdf |
| Esquema Waveshare | [Esquema](Waveshare_schematic.pdf) | https://www.waveshare.com/w/upload/5/54/MCP23017-IO-Expansion-Board-Schematic.pdf |
| Manual Waveshare | [Manual](Waveshare_user_manual.pdf) | https://www.waveshare.com/w/upload/8/8e/MCP2307_IO_Expansion_Board_User_Manual_EN.pdf |

Pagina del modulo: https://www.waveshare.com/wiki/MCP23017_IO_Expansion_Board

El nombre original del manual contiene MCP2307; es el enlace publicado
por Waveshare para esta placa. Comparar la revision del esquema con la
placa entregada antes de asumir orden de pines o componentes.
Segun la guia, A0/A1/A2 abiertos son altos y cortocircuitados son bajos:
todos abiertos seleccionan 0x27 y todos cerrados 0x20.