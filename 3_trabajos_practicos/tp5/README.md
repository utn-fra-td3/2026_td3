# tp5

Este trabajo práctico debe realizarse en el archivo [kernel_module.c](./kernel_module.c). Se debe entregar el directorio completo ignorando cualquier archivo resultante de la compilación del módulo de kernel que va a generarse dentro de `build`.

Crear una rama nueva llamada `tp5/v1`. Se puede hacer con el comando:

```bash
git checkout -b tp5/v1
```

## Consigna

Implementar un blinky en el kernel de Linux.

> :warning: Para este trabajo práctico, es necesario contar con:
> * Una Raspberry Pi Zero 2W/4/5 con el kernel v6.12.34 o similar
> * Un LED + resistencia de polarización y cables de conexión

### Primera versión

Desarrollar un módulo que, al cargarlo en el kernel, cree dos hilos que corren de forma periódica cada medio segundo, imprimiendo como mensaje en el kernel _Hola desde el kernel!_ y _Chau desde el kernel!_ respectivamente.

Para ver los mensajes del kernel usar el comando:

```bash
dmesg -wHT
```

Es posible desarrollar esta parte del trabajo práctico en una máquina virtual.

Una vez resuelta la consigna, hacer un commit de los cambios:

```bash
git add kernel_module.c
git commit -m "Primer consigna de TP5"
```

### Segunda versión

Cambiar los mensajes del kernel por un on y off de un LED. La inicialización del GPIO debe hacerse al cargarse el módulo y el on y off en las tareas. 

El GPIO a usar debe seleccionarse al momento de cargar el módulo en el kernel pasándolo como parámetro.

Como ayuda, se proveen las bibliotecas [gpio_driver.h](./gpio_driver.h) y [gpio_driver.c](./gpio_driver.c) para simplificar el uso del hardware.

![Raspberry Pi pinout](https://www.raspberrypi.com/documentation/computers/images/GPIO-Pinout-Diagram-2.png?hash=df7d7847c57a1ca6d5b2617695de6d46)

Una vez resuelta la consigna, hacer un commit de los cambios:

```bash
git add kernel_module.c
git commit -m "Segunda consigna de TP5"
```

## Entrega

Pushear la rama `tp5/v1` al repositorio personal.

```bash
git push origin tp5/v1
```

Luego, crear un pull request a la rama correspondiente del repositorio de la cátedra con el título del pull request de _Entrega de TP5_.