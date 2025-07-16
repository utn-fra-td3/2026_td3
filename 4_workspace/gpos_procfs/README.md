# 07_procfs

Ejemplo basico de uso de un proc file y poder escribir y leerlo.

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

4- Para ver los mensajes del kernel y verificar que se imprimen los mensajes usamos:

```bash
make dmesg
```

5- Para verificar que el proc file fue creado podemos escribir:

```bash
ls /proc/utn-fra-td3
```

6- Para escribir sobre el proc file podemos usar el comando `echo`:

```bash
echo "Escribiendo..." > /proc/utn-fra-td3/test
```

7- Para leer su contenido podemos usar `cat`:

```bash
cat /proc/utn-fra-td3/test
```
