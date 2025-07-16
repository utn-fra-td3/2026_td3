# Etiqueta para definir el nombre del archivo con el codigo del modulo
FILE := char_dev_ops
# Define el autor del modulo (coincide con el del codigo del modulo)
AUTHOR := utn-fra-td3
# Directorio donde se encuentra el kernel
KDIR := /lib/modules/$(shell uname -r)/build
# Directorio destino de archivos de compilacion
MDIR := $(PWD)/build

# Codigo fuente para compilar
obj-m += ${FILE}.o

# Compilacion completa
all:
	mkdir -p build;					\
	cp -f ${FILE}.c Makefile ${MDIR};	\
	cd build;									\
	make -C ${KDIR} M=${MDIR} modules

# Limpia el directorio de compilacion
clean:
	@cd build;							\
	make -C ${KDIR} M=${MDIR} clean;	\
	cd ..;	\
	rm -rf build

# Carga el modulo en el kernel
insmod:
	@sudo insmod build/${FILE}.ko
	@sudo chmod 666 /dev/${AUTHOR}

# Quita el modulo del kernel
rmmod:
	@sudo rmmod ${FILE}

# Muestra los mensajes del kernel que imprimio este modulo
dmesg:
	@dmesg -T | grep ${AUTHOR}

# Muestra ayuda para el Makefile
help:
	@echo "all: Compila el modulo del kernel desarrollado (por defecto)"
	@echo "clean: Limpia los resultados de la compilacion"
	@echo "insmod: Carga el modulo de kernel"
	@echo "rmmod: Remueve el modulo de kernel"
	@echo "help: Muestra esta ayuda"
