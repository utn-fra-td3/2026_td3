#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/ioctl.h>

#include "osc.h"

#define DEV_PATH "/dev/osc"

static int fd;
static pthread_mutex_t consola = PTHREAD_MUTEX_INITIALIZER;
static volatile bool ver_trama = false;
static volatile bool ver_monitor = true;

static bool es_linea_de_trama(const char *l)
{
    return !strncmp(l, "START", 5) || !strncmp(l, "END", 3) ||
           !strncmp(l, "CONF:", 5) || !strncmp(l, "TRIG:", 5) ||
           l[0] == '-' || isdigit((unsigned char)l[0]);
}

static void *hilo_monitor(void *arg)
{
    (void)arg;
    char buf[256], linea[128];
    size_t len = 0;
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] != '\n') {
                if (len < sizeof(linea) - 1)
                    linea[len++] = buf[i];
                continue;
            }
            linea[len] = '\0';
            len = 0;

            if (!ver_monitor || (!ver_trama && es_linea_de_trama(linea)))
                continue;

            pthread_mutex_lock(&consola);
            printf("  [esp] %s\n", linea);
            fflush(stdout);
            pthread_mutex_unlock(&consola);
        }
    }
    return NULL;
}

static const char *explicar_error(int e)
{
    switch (e) {
    case EINVAL:    return "valor fuera de rango";
    case EPERM:     return "variable de solo lectura";
    case ENOENT:    return "variable desconocida";
    case EIO:       return "el ESP no pudo aplicar el cambio";
    case ETIMEDOUT: return "el ESP no respondio (cableado? firmware?)";
    case EPROTO:    return "respuesta con formato inesperado";
    default:        return strerror(e);
    }
}

static int hacer_ioctl(unsigned long cmd, const char *nombre, int *valor)
{
    struct osc_var var;

    memset(&var, 0, sizeof(var));
    strncpy(var.nombre, nombre, sizeof(var.nombre) - 1);
    var.valor = *valor;

    if (ioctl(fd, cmd, &var) < 0)
        return errno;

    *valor = var.valor;
    return 0;
}

static void cmd_get(const char *nombre)
{
    int v = 0, e = hacer_ioctl(OSC_GET, nombre, &v);

    pthread_mutex_lock(&consola);
    if (e) printf("get %s: ERROR, %s\n", nombre, explicar_error(e));
    else   printf("%s = %d\n", nombre, v);
    pthread_mutex_unlock(&consola);
}

static void cmd_set(const char *nombre, int valor)
{
    int v = valor, e = hacer_ioctl(OSC_SET, nombre, &v);

    pthread_mutex_lock(&consola);
    if (e) printf("set %s %d: ERROR, %s\n", nombre, valor, explicar_error(e));
    else   printf("OK, %s = %d (confirmado por el ESP)\n", nombre, v);
    pthread_mutex_unlock(&consola);
}

static void cmd_estado(void)
{
    static const char *vars[] = { "estado", "escala", "base", "flanco", "trig_mv", "stream" };

    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); i++)
        cmd_get(vars[i]);
}

static void ayuda(void)
{
    printf(
        "\nComandos:\n"
        "  get VARIABLE        estado | escala | base | flanco | trig_mv | stream\n"
        "  set VARIABLE VALOR  escala 0/1 | base 0/1/2 | flanco 0/1\n"
        "  exec ACCION         stream_on | stream_off\n"
        "  estado              lee todas las variables\n"
        "  trama on|off        mostrar u ocultar la trama de medicion\n"
        "  monitor on|off      mostrar u ocultar todo lo que llega del ESP\n"
        "  ayuda | salir\n\n");
}

int main(void)
{
    pthread_t monitor;
    char linea[64], a[16], b[16];
    int valor;

    fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) {
        perror("open " DEV_PATH " (esta cargado el modulo?)");
        return 1;
    }

    pthread_create(&monitor, NULL, hilo_monitor, NULL);
    ayuda();

    while (printf("osc> "), fflush(stdout), fgets(linea, sizeof(linea), stdin)) {
        if (sscanf(linea, "set %15s %d", a, &valor) == 2) {
            cmd_set(a, valor);
        } else if (sscanf(linea, "get %15s", a) == 1) {
            cmd_get(a);
        } else if (sscanf(linea, "exec %15s", a) == 1) {
            char msg[32];
            snprintf(msg, sizeof(msg), "exec %s\n", a);
            if (write(fd, msg, strlen(msg)) < 0)
                perror("write");
            usleep(200000);   /* dar tiempo a que el monitor muestre la respuesta */
        } else if (sscanf(linea, "trama %15s", b) == 1) {
            ver_trama = !strcmp(b, "on");
        } else if (sscanf(linea, "monitor %15s", b) == 1) {
            ver_monitor = !strcmp(b, "on");
        } else if (!strncmp(linea, "estado", 6)) {
            cmd_estado();
        } else if (!strncmp(linea, "salir", 5)) {
            break;
        } else if (linea[0] != '\n') {
            ayuda();
        }
    }

    close(fd);
    return 0;
}
