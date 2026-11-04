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

/* Variables que entiende el ESP32 */
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

static void imprimir_valor(const char *nombre, int valor)
{
    if (strcmp(nombre, "M") == 0) {
        const char *modos[] = {"X1 (CH5)", "X10 (CH6)", "AC (CH2)"};
        if (valor >= 0 && valor <= 2)
            fprintf(salida_texto, "  M = %d -> Modo: %s\n", valor, modos[valor]);
        else
            fprintf(salida_texto, "  M = %d -> (Desconocido)\n", valor);

    } else if (strcmp(nombre, "F") == 0) {
        if (valor >= 0 && valor <= 1)
            fprintf(salida_texto, "  F = %d -> Flanco: %s\n", valor, valor == 0 ? "Subida" : "Bajada");
        else
            fprintf(salida_texto, "  F = %d -> (Desconocido)\n", valor);

    } else if (strcmp(nombre, "T") == 0) {
        const char *tiempos[] = {"0.5 ms/div", "1.0 ms/div", "2.0 ms/div", "5.0 ms/div"};
        if (valor >= 0 && valor <= 3)
            fprintf(salida_texto, "  T = %d -> Tiempo: %s\n", valor, tiempos[valor]);
        else
            fprintf(salida_texto, "  T = %d -> (Desconocido)\n", valor);

    } else if (strcmp(nombre, "A") == 0) {
        const char *amps[] = {"1.0 V/div", "5.0 V/div"};
        if (valor >= 0 && valor <= 1)
            fprintf(salida_texto, "  A = %d -> Amplitud: %s\n", valor, amps[valor]);
        else
            fprintf(salida_texto, "  A = %d -> (Desconocido)\n", valor);

    } else if (strcmp(nombre, "L") == 0) {
        float v_trigger = valor * (3.3 / 4095.0);
        fprintf(salida_texto, "  L = %d -> Nivel Trigger: %.2f V\n", valor, v_trigger);

    } else {
        fprintf(salida_texto, "  %s = %d\n", nombre, valor);
    }
}

static void mostrar_uso(void)
{
    fprintf(salida_texto,
            "Comandos:\n"
            "  set <M|F|T|A|L> <valor>   M 0|1|2  F 0|1  T 0..3  A 0|1  L 0..4095\n"
            "  get <M|F|T|A|L>           muestra el valor real de una variable\n"
            "  exec state                muestra la configuracion completa\n"
            "  exec default              restablece la configuracion predeterminada\n"
            "  salir\n");
}

static void mostrar_estado(void)
{
    int valor;
    size_t i;

    fprintf(salida_texto, "\n--- ESTADO ACTUAL DEL OSCILOSCOPIO ---\n");
    for (i = 0; i < N_VARIABLES; i++) {
        if (hacer_get(VARIABLES[i], &valor) == 0) {
            imprimir_valor(VARIABLES[i], valor);
        } else {
            explicar_error_get(VARIABLES[i]);
            return;
        }
    }
    fprintf(salida_texto, "--------------------------------------\n\n");
}

/* =====================================================================
 * NUEVO: Función para restablecer a valores predeterminados
 * ===================================================================== */
static void aplicar_predeterminado(void)
{
    fprintf(salida_texto, "\nAplicando configuracion predeterminada...\n");
    hacer_set("M", 0);    /* Modo X1 */
    usleep(100000);
    hacer_set("F", 1);    /* Flanco Bajada */
    usleep(100000);
    hacer_set("T", 1);    /* Tiempo 1.0 ms/div */
    usleep(100000);
    hacer_set("A", 0);    /* Amplitud 1.0 V/div */
    usleep(100000);
    hacer_set("L", 2000); /* Nivel Trigger a mitad de escala */
    usleep(100000);
    fprintf(salida_texto, "Configuracion lista.\n");
    mostrar_estado();
}

int main(int argc, char *argv[])
{
    pthread_t monitor;
    char linea[64], nombre[ESP32_VAR_NAME_MAX], accion[16];
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
            if (hacer_get(nombre, &valor) == 0) {
                imprimir_valor(nombre, valor);
            }
            else
                explicar_error_get(nombre);

        /* Interceptamos el comando exec */
        } else if (sscanf(linea, "exec %15s", accion) == 1) {
            for(int i = 0; accion[i]; i++) accion[i] = tolower((unsigned char)accion[i]);
            
            if (strcmp(accion, "default") == 0) {
                aplicar_predeterminado();
            } else if (strcmp(accion, "state") == 0) {
                mostrar_estado();
            } else {
                fprintf(salida_texto, "Accion '%s' no reconocida. Usa 'exec default' o 'exec state'\n", accion);
            }

        } else {
            mostrar_uso();
        }
        fflush(salida_texto);
    }

    close(fd);
    return 0;
}