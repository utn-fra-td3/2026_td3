MODULE := ega_uart
OVERLAY := ega_uart_overlay
KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m += $(MODULE).o

.PHONY: all module overlay clean load unload install-rules

all: module overlay

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

overlay: $(OVERLAY).dtbo

$(OVERLAY).dtbo: $(OVERLAY).dts
	dtc -@ -I dts -O dtb -o $@ $<

load: all
	sudo dtoverlay -d $(CURDIR) $(OVERLAY)
	sudo insmod $(MODULE).ko

unload:
	-sudo rmmod $(MODULE)
	-sudo dtoverlay -r $(OVERLAY)

install-rules:
	sudo install -m 0644 99-ega-uart.rules /etc/udev/rules.d/99-ega-uart.rules
	sudo udevadm control --reload-rules
	sudo udevadm trigger

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	rm -f $(OVERLAY).dtbo
