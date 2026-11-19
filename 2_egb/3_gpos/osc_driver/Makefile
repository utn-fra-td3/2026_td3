# Modulo de kernel del osciloscopio (Kbuild)
FILE := osc

# Headers del kernel que esta corriendo
KDIR := /lib/modules/$(shell uname -r)/build
# Directorio de compilacion (para no ensuciar la carpeta)
MDIR := $(PWD)/build

obj-m += $(FILE).o

all: modulo overlay

modulo:
	@mkdir -p build
	cp -f $(FILE).c osc.h Makefile $(MDIR)
	$(MAKE) -C $(KDIR) M=$(MDIR) modules

overlay:
	dtc -@ -I dts -O dtb -o osc.dtbo osc-pi4-overlay.dts

clean:
	rm -rf build osc.dtbo
