#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "Ejecutar con: sudo ./install_raspberry.sh" >&2
    exit 1
fi

BASE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
KERNEL_VERSION=$(uname -r)
BOOT_DIR=/boot/firmware
if [ ! -d "$BOOT_DIR" ]; then
    BOOT_DIR=/boot
fi

make -C "$BASE_DIR/3_gpos" all
make -C "$BASE_DIR/4_apps/cli" all

test -f "$BASE_DIR/3_gpos/ega_uart.ko"
test -f "$BASE_DIR/3_gpos/ega_uart_overlay.dtbo"
test -x "$BASE_DIR/4_apps/cli/ega-cli"

raspi-config nonint do_serial_cons 1
raspi-config nonint do_serial_hw 0

if ! grep -qxF "enable_uart=1" "$BOOT_DIR/config.txt"; then
    printf '\n# EGB: UART para la fuente EGA\nenable_uart=1\n' >> "$BOOT_DIR/config.txt"
fi
if ! grep -qxF "dtoverlay=ega_uart_overlay" "$BOOT_DIR/config.txt"; then
    printf 'dtoverlay=ega_uart_overlay\n' >> "$BOOT_DIR/config.txt"
fi

install -m 0644 "$BASE_DIR/3_gpos/ega_uart_overlay.dtbo" \
    "$BOOT_DIR/overlays/ega_uart_overlay.dtbo"
install -d "/lib/modules/$KERNEL_VERSION/extra"
install -m 0644 "$BASE_DIR/3_gpos/ega_uart.ko" \
    "/lib/modules/$KERNEL_VERSION/extra/ega_uart.ko"
printf 'ega_uart\n' > /etc/modules-load.d/ega-uart.conf
depmod -a

install -m 0644 "$BASE_DIR/3_gpos/99-ega-uart.rules" \
    /etc/udev/rules.d/99-ega-uart.rules
udevadm control --reload-rules

install -m 0755 "$BASE_DIR/4_apps/cli/ega-cli" /usr/local/bin/ega-cli
install -d -m 0755 /opt/ega-egb
install -m 0755 "$BASE_DIR/4_apps/gui/ega_web.py" /opt/ega-egb/ega_web.py
install -m 0644 "$BASE_DIR/4_apps/gui/ega-web.service" \
    /etc/systemd/system/ega-web.service
systemctl daemon-reload
systemctl enable ega-web.service

echo "Instalacion completa. Reiniciar con: sudo reboot"
echo "Luego abrir: http://$(hostname).local:8080"
