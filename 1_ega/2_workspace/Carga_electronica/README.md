# workspace

En este directorio, se deben subir todas las bibliotecas que se usen (sensores, pantallas, actuadores, etc) y un directorio de [firmware](./firmware) donde debe desarrollarse el programa del microcontrolador.

En firmware, siempre ignorar directorio de archivos de compilación (_debug_ o _build_) con el _.gitignore_.

En este directorio ya está agregada la biblioteca de [freertos](./freertos) para implementar en el firmware.