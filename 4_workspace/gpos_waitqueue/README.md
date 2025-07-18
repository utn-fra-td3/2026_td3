# 06_waitqueue

Ejemplo basico de uso de colas para despertar tareas cuando se dan las condiciones.

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
