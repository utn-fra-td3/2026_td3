# 01_hello_kernel

Ejemplo basico que imprime mensajes en el kernel cuando el modulo se carga y se quita del kernel.

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

4- Para ver los mensajes del kernel y verificar que estan ambos mensajes, usamos:

```bash
make dmesg
```
