FILE := ega_link

KDIR := /lib/modules/$(shell uname -r)/build
MDIR := $(PWD)/build

obj-m += $(FILE).o

all:
	mkdir -p build
	cp -f $(FILE).c Makefile build/
	$(MAKE) -C $(KDIR) M=$(MDIR) modules

clean:
	rm -rf build
