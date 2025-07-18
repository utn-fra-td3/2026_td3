# 02_kthread_v1

Ejemplo basico que imprime mensajes periodicamente en el kernel a traves de dos tareas que corren mientras el modulo permanezca cargado en el kernel.

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
