# Etiqueta para definir el nombre del archivo con el codigo del modulo
FILE := esp32_link
# @dmesg -wHT | grep ${AUTHOR}

# Directorio donde se encuentra el kernel
KDIR := /lib/modules/$(shell uname -r)/build
# Directorio destino de archivos de compilacion
MDIR := $(PWD)/build

# Codigo fuente para compilar
obj-m += ${FILE}.o

# Compilacion completa
all:
	@mkdir -p build;						\
	cp -f ${FILE}.c esp32_link.h Makefile ${MDIR};	\
	cd build;							\
	make -C ${KDIR} M=${MDIR} modules

# Limpia el directorio de compilacion
clean:
	@cd build;							\
	make -C ${KDIR} M=${MDIR} clean;	\
	cd ..;	\
	rm -rf build
