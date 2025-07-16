# 04_char_dev

Ejemplo basico que imprime mensajes en el kernel con dos tareas haciendo uso de la exclusion mutua.

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

4- Para ver los mensajes del kernel, usamos:

```bash
make dmesg
```

5- Para verificar que el char device fue creado, usamos:

```bash
ls /dev/utn-fra-td3
```
