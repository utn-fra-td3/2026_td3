# 05_mutex

Ejemplo basico que imprime mensajes periodicamente mediante dos tareas creadas a partir de la misma funcion y que usan la idea de exclusion mutua para no escribir en el kernel al mismo tiempo.

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
