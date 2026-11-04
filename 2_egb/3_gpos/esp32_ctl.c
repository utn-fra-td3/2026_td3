#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "esp32_link.h"

#define DEV_PATH "/dev/esp32link"

static int   fd;
static FILE *salida_texto;   /* stdout en modo interactivo, stderr en modo puente */

/* Variables que entiende el ESP32 (mismas letras que la consola de Python) */
static const char *VARIABLES[] = { "M", "F", "T", "A", "L" };
#define N_VARIABLES (sizeof(VARIABLES) / sizeof(VARIABLES[0]))

/* ---------------------------------------------------------------------
 * MODO PUENTE (por defecto): lo lanza osciloscopio.py por SSH.
 *   stdout -> bytes crudos del ESP32 (tramas binarias). NO se imprime
 *             ningun texto ahi, o se corrompe el stream.
 *   stdin  -> lineas "set M 1" / "get M"
 *   stderr -> mensajes y respuestas de get
 * MODO INTERACTIVO (-i): para usarlo a mano en la Pi.
 *   no reenvia el stream; todo el texto va a stdout.
 * --------------------------------------------------------------------- */

static void *hilo_monitor(void *arg)
{
    unsigned char buf[512];
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t off = 0;
        while (off < n) {                       /* write() puede ser parcial */
            ssize_t w = write(STDOUT_FILENO, buf + off, n - off);
            if (w < 0) {
                if (errno == EINTR) continue;
                return NULL;                    /* stdout cerrado: la sesion SSH termino */
            }
            off += w;
        }
    }
    return NULL;
}

static int hacer_set(const char *nombre, int valor)
{
    struct esp32_var var;

    memset(&var, 0, sizeof(var));
    strncpy(var.nombre, nombre, sizeof(var.nombre) - 1);
    var.valor = valor;
    return ioctl(fd, ESP32_SET, &var);
}

static int hacer_get(const char *nombre, int *valor)
{
    struct esp32_var var;

    memset(&var, 0, sizeof(var));
    strncpy(var.nombre, nombre, sizeof(var.nombre) - 1);
    if (ioctl(fd, ESP32_GET, &var) < 0)
        return -1;
    *valor = var.valor;
    return 0;
}

static void explicar_error_get(const char *nombre)
{
    switch (errno) {
    case ETIMEDOUT: fprintf(salida_texto, "get %s: el ESP32 no esta mandando tramas\n", nombre); break;
    case EINVAL:    fprintf(salida_texto, "get %s: variable desconocida (usar M, F, T, A o L)\n", nombre); break;
    case EPROTO:    fprintf(salida_texto, "get %s: valor fuera de las tablas conocidas\n", nombre); break;
    default:        fprintf(salida_texto, "get %s: error: %s\n", nombre, strerror(errno)); break;
    }
}

static void mostrar_uso(void)
{
    fprintf(salida_texto,
            "Comandos:\n"
            "  set <M|F|T|A|L> <valor>   M 0|1|2  F 0|1  T 0..3  A 0|1  L 0..4095\n"
            "  get <M|F|T|A|L>\n"
            "  estado                    muestra las 5 variables\n"
            "  salir\n");
}

static void mostrar_estado(void)
{
    int valor;
    size_t i;

    for (i = 0; i < N_VARIABLES; i++) {
        if (hacer_get(VARIABLES[i], &valor) == 0)
            fprintf(salida_texto, "%s = %d\n", VARIABLES[i], valor);
        else {
            explicar_error_get(VARIABLES[i]);
            return;     /* si no hay tramas, no tiene sentido pedir las demas */
        }
    }
}

int main(int argc, char *argv[])
{
    pthread_t monitor;
    char linea[64], nombre[ESP32_VAR_NAME_MAX];
    int valor, interactivo = 0;

    if (argc > 1 && !strcmp(argv[1], "-i"))
        interactivo = 1;
    else if (argc > 1) {
        fprintf(stderr, "Uso: %s [-i]\n  (sin argumentos: modo puente para osciloscopio.py)\n", argv[0]);
        return 1;
    }

    salida_texto = interactivo ? stdout : stderr;

    fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Error: no se pudo abrir %s: %s\n", DEV_PATH, strerror(errno));
        return 1;
    }

    if (!interactivo)
        pthread_create(&monitor, NULL, hilo_monitor, NULL);
    else
        mostrar_uso();

    while (fgets(linea, sizeof(linea), stdin)) {
        if (!strncmp(linea, "salir", 5))
            break;

        if (sscanf(linea, "set %15s %d", nombre, &valor) == 2) {
            nombre[0] = toupper((unsigned char)nombre[0]);
            if (hacer_set(nombre, valor) < 0)
                fprintf(salida_texto, "set %s %d: error: %s\n", nombre, valor, strerror(errno));
            else if (interactivo)
                fprintf(salida_texto, "set %s %d enviado\n", nombre, valor);

        } else if (sscanf(linea, "get %15s", nombre) == 1) {
            nombre[0] = toupper((unsigned char)nombre[0]);
            if (hacer_get(nombre, &valor) == 0)
                fprintf(salida_texto, "%s = %d\n", nombre, valor);
            else
                explicar_error_get(nombre);

        } else if (!strncmp(linea, "estado", 6)) {
            mostrar_estado();

        } else {
            mostrar_uso();
        }
        fflush(salida_texto);
    }

    close(fd);
    return 0;
}