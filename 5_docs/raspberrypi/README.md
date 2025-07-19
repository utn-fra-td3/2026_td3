# Raspberry Pi

Dejamos en este directorio algo de documentación acerca de las Raspberry Pi que vamos a estar usando en la cátedra. 

## Hardware

Los modelos de Raspberry Pi que podemos usar incluyen:

* [Raspberry Pi Zero 2W](https://www.raspberrypi.com/products/raspberry-pi-zero-2-w/)
* [Raspberry Pi 4](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/)
* [Raspberry Pi 5](https://www.raspberrypi.com/products/raspberry-pi-5/)

En cualquiera de los casos, necesitaremos una tarjeta SD de por lo menos 8 GB con la última versión de Raspberry Pi OS Lite (64-bit) que podemos grabar con [Raspberry Pi Imager](https://www.raspberrypi.com/software/) en la tarjeta. Vamos a estar usando la versión Lite del OS ya que no tiene interfaz gráfica, haciendo que sea más liviana la imagen y menos recursos se inviertan en eso.

> :warning: A la hora de configurar la instalación con Raspberry Pi Imager, tomar nota del hostname, usuario y contraseña ya que lo necesitaremos para acceder. También, es importante habilitar el servidor SSH y la conexión WiFi para poder conectarnos con nuestra computadora sin hardware extra.

## Setup

Antes de empezar con cualquier configuración, es necesario tener un cliente SSH. Una vez abierto el cliente y estando en la misma red, nos conectamos escribiendo:

```
ssh USER@HOSTNAME.local
```

Reemplazando `USER` y `HOSTNAME` por el usuario y hostname de nuestra Raspberry Pi. Si todo está correcto, se nos va a solicitar la contraseña y podemos ingresar.

### Primera instalación

Si la Raspberry Pi tiene el OS limpio recién instalado, vamos a necesitar algunas actualizaciones. Para eso, escribimos en la terminal:

```bash
sudo apt-get update && sudo apt-get upgrade -y
```

Luego, instalamos los siguientes paquetes:

```bash
sudo apt install build-essential bc bison flex libncurses5-dev libssl-dev -y
```

Por último, instalamos los headers del kernel de Linux:

```bash
sudo apt install raspberrypi-kernel-headers -y
```