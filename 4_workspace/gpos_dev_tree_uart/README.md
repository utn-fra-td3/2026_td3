# 10_dev_tree_uart

Ejemplo básico que usa un un char device para escribir y leer por UART.

## Como usar

1- Dentro de este directorio, compilar el modulo usando el comando:

```bash
make
```

2- Para cargarlo en el kernel, podemos usar:

```bash
make insmod
```

3- Para sacarlo del kernel, usamos:

```bash
make rmmod
```

4- Para ver los mensajes del kernel y usamos:

```bash
make dmesg
```

5- Para verificar que el char device fue creado podemos escribir:

```bash
ls /dev/utn-fra-td3
```

6- Para escribir sobre el char device podemos usar el comando `echo`:

```bash
echo "Escribiendo..." > /dev/utn-fra-td3
```

7- Para leer su contenido podemos usar `cat`:

```bash
cat /dev/utn-fra-td3
```