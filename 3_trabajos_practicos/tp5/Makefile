# Nombre del modulo del kernel
MNAME := utn-fra-td3-tp5
# Directorio donde se encuentra el kernel
KDIR := /lib/modules/$(shell uname -r)/build
# Directorio destino de archivos de compilacion
MDIR := $(PWD)/build

# Codigo fuente para compilar
obj-m += ${MNAME}.o
${MNAME}-objs := kernel_module.o gpio_driver.o

# Compilacion completa
all:
	@mkdir -p build;			\
	rsync -a --exclude 'build/' . build/;	\
	cd build;				\
	make -C ${KDIR} M=${MDIR} modules

# Limpia el directorio de compilacion
clean:
	@cd build;	\
	make -C ${KDIR} M=${MDIR} clean;	\
	cd ..;	\
	rm -rf build
