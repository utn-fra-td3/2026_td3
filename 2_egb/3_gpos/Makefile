# Etiqueta para definir el nombre del archivo con el codigo del modulo
FILE := esp32_link
# Etiqueta para definir el nombre del programa de usuario (control del ecualizador)
CTL  := eq_ctl
# @dmesg -wHT | grep ${AUTHOR}

# Directorio donde se encuentra el kernel
KDIR := /lib/modules/$(shell uname -r)/build
# Directorio destino de archivos de compilacion
MDIR := $(PWD)/build

# Codigo fuente para compilar
obj-m += ${FILE}.o

# Compilacion completa: modulo de kernel + programa de usuario
all: module ctl

# Modulo de kernel (esp32_link.ko)
module:
	@mkdir -p build;						\
	cp -f ${FILE}.c esp32_link.h Makefile ${MDIR};	\
	cd build;							\
	make -C ${KDIR} M=${MDIR} modules

# Programa de usuario (eq_ctl): usa pthread y el header compartido con el driver
ctl:
	gcc -Wall -pthread -o ${CTL} ${CTL}.c

# Limpia el directorio de compilacion y el ejecutable
clean:
	@cd build;							\
	make -C ${KDIR} M=${MDIR} clean;	\
	cd ..;	\
	rm -rf build ${CTL}

.PHONY: all module ctl clean
