#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>

#define DEFAULT_DEVICE "/dev/ega_uart"
#define DEFAULT_TIMEOUT_MS 1500
#define COMMAND_SIZE 64
#define RESPONSE_SIZE 512

static void usage(const char *program) {
    printf("Uso: %s [-d dispositivo] [-t timeout_ms] [COMANDO ...]\n", program);
    puts("\nComandos EGA:");
    puts("  GET              Lee mediciones, consignas, estados y fallas");
    puts("  SET P <V>        Fija la tension de la salida positiva");
    puts("  SET N <V>        Fija la magnitud de la salida negativa");
    puts("  SET ILP <A>      Fija el limite de corriente positivo (0.10-0.75 A)");
    puts("  SET ILN <A>      Fija el limite de corriente negativo (0.10-0.75 A)");
    puts("  SET VLP <V>      Fija el limite de sobretension positivo (5-40 V)");
    puts("  SET VLN <V>      Fija el limite de sobretension negativo (5-40 V)");
    puts("  OUT P ON|OFF     Habilita o deshabilita la salida positiva");
    puts("  OUT N ON|OFF     Habilita o deshabilita la salida negativa");
    puts("  RESET P|N        Borra fallas y mantiene la salida apagada");
    puts("  SAVE             Guarda la configuracion (salidas apagadas)");
    puts("\nSin COMANDO se inicia el modo interactivo. Use HELP o EXIT.");
}

static int device_open(const char *device) {
    int fd = open(device, O_RDWR);

    if (fd < 0) {
        fprintf(stderr, "No se pudo abrir %s: %s\n", device, strerror(errno));
        return -1;
    }
    return fd;
}

static void trim(char *text) {
    char *start = text;
    size_t length;

    while (isspace((unsigned char)*start)) {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1U);
    }
    length = strlen(text);
    while (length > 0U && isspace((unsigned char)text[length - 1U])) {
        text[--length] = '\0';
    }
}

static void uppercase(char *text) {
    for (; *text != '\0'; text++) {
        *text = (char)toupper((unsigned char)*text);
    }
}

static int translate_command(const char *command, char *wire, size_t capacity) {
    char input[COMMAND_SIZE];
    if (strlen(command) >= sizeof(input)) return -1;
    strcpy(input, command);
    uppercase(input);

    const char *spaces = " \t\r\n";
    char *verb = strtok(input, spaces);
    char *key = strtok(NULL, spaces);
    char *arg = strtok(NULL, spaces);
    if (!verb || strtok(NULL, spaces)) return -1;

    int length;
    if (!key && (!strcmp(verb, "GET") || !strcmp(verb, "SAVE"))) {
        length = snprintf(wire, capacity, "%s", verb);
    } else if (key && (!strcmp(verb, "OUT") || !strcmp(verb, "RESET"))) {
        if (strcmp(key, "P") && strcmp(key, "N")) return -1;
        int branch = key[0] == 'N';
        if (!strcmp(verb, "RESET") && !arg) {
            length = snprintf(wire, capacity, "RST %d", branch);
        } else if (!strcmp(verb, "OUT") && arg && (!strcmp(arg, "ON") || !strcmp(arg, "OFF"))) {
            length = snprintf(wire, capacity, "OUT %d %s", branch, arg);
        } else {
            return -1;
        }
    } else if (!strcmp(verb, "SET") && key && arg) {
        char *end;
        double value = strtod(arg, &end);
        if (*end) return -1;
        const char *setting;
        if (!strcmp(key, "P") || !strcmp(key, "N")) {
            if (!(value >= 0.0 && value <= 30.0)) return -1;
            setting = "V";
        } else if (!strcmp(key, "ILP") || !strcmp(key, "ILN")) {
            if (!(value >= 0.10 && value <= 0.75)) return -1;
            int steps = (int)(value * 20.0 + 0.5);
            double delta = value - steps / 20.0;
            if (delta < -0.000001 || delta > 0.000001) return -1;
            setting = "IL";
        } else if (!strcmp(key, "VLP") || !strcmp(key, "VLN")) {
            if (!(value >= 5.0 && value <= 40.0)) return -1;
            setting = "VL";
        } else {
            return -1;
        }
        int branch = key[strlen(key) - 1] == 'N';
        length = snprintf(wire, capacity, "%s %d %s", setting, branch, arg);
    } else {
        return -1;
    }
    return length >= 0 && (size_t)length < capacity ? 0 : -1;
}

static bool is_async_notification(const char *line) {
    return strncmp(line, "PROT ", 5) == 0;
}

static int write_all(int fd, const char *data, size_t length) {
    while (length > 0U) {
        ssize_t written = write(fd, data, length);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        data += written;
        length -= (size_t)written;
    }
    return 0;
}

static int64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int exchange_command(int fd, const char *command, int timeout_ms,
                            char *response, size_t response_size) {
    struct pollfd descriptor = {.fd = fd, .events = POLLIN};
    size_t used = 0U;

    if (write_all(fd, command, strlen(command)) != 0 || write_all(fd, "\n", 1U) != 0) {
        fprintf(stderr, "Error enviando el comando: %s\n", strerror(errno));
        return -1;
    }
    int64_t start = monotonic_ms();
    if (start < 0) return -1;
    while (used + 1U < response_size) {
        int64_t now = monotonic_ms();
        if (now < 0) return -1;
        int64_t remaining = start + timeout_ms - now;
        int ready = remaining > 0 ? poll(&descriptor, 1, (int)remaining) : 0;
        if (ready == 0) {
            fprintf(stderr, "Timeout: la EGA no respondio en %d ms\n", timeout_ms);
            return -1;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "Error esperando la respuesta: %s\n", strerror(errno));
            return -1;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fputs("El dispositivo UART dejo de estar disponible\n", stderr);
            return -1;
        }
        if ((descriptor.revents & POLLIN) != 0) {
            char byte;
            ssize_t count = read(fd, &byte, 1U);
            if (count < 0 && errno != EINTR) {
                fprintf(stderr, "Error leyendo la respuesta: %s\n", strerror(errno));
                return -1;
            }
            if (count == 1 && byte != '\r') {
                if (byte == '\n') {
                    response[used] = '\0';
                    if (is_async_notification(response)) {
                        fprintf(stderr, "%s\n", response);
                        used = 0;
                        continue;
                    }
                    return 0;
                }
                response[used++] = byte;
            }
        }
    }

    response[used] = '\0';
    fputs("La respuesta UART supera el tamano admitido\n", stderr);
    return -1;
}

static int run_command(int fd, char *command, int timeout_ms) {
    char response[RESPONSE_SIZE];

    trim(command);
    uppercase(command);
    char wire[COMMAND_SIZE];
    if (translate_command(command, wire, sizeof(wire)) != 0) {
        fprintf(stderr, "Comando invalido: %s\nEscriba HELP para ver la lista.\n", command);
        return -1;
    }
    if (exchange_command(fd, wire, timeout_ms, response, sizeof(response)) != 0) {
        return -1;
    }
    puts(response);
    return strncmp(response, "OK", 2U) == 0 ? 0 : 1;
}

static int interactive_mode(int fd, int timeout_ms) {
    char command[COMMAND_SIZE];

    puts("Cliente UART EGA conectado. HELP muestra los comandos; EXIT finaliza.");
    while (true) {
        fputs("ega> ", stdout);
        fflush(stdout);
        if (fgets(command, sizeof(command), stdin) == NULL) {
            putchar('\n');
            return 0;
        }
        trim(command);
        uppercase(command);
        if (strcmp(command, "EXIT") == 0 || strcmp(command, "QUIT") == 0) {
            return 0;
        }
        if (strcmp(command, "HELP") == 0) {
            usage("ega-cli");
            continue;
        }
        if (command[0] != '\0') {
            run_command(fd, command, timeout_ms);
        }
    }
}

#ifndef EGA_CLI_TEST
int main(int argc, char **argv) {
    const char *device = DEFAULT_DEVICE;
    int timeout_ms = DEFAULT_TIMEOUT_MS;
    int option;
    int fd;
    int result;

    while ((option = getopt(argc, argv, "d:t:h")) != -1) {
        switch (option) {
            case 'd':
                device = optarg;
                break;
            case 't':
                timeout_ms = atoi(optarg);
                if (timeout_ms <= 0) {
                    fputs("El timeout debe ser mayor que cero\n", stderr);
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
                usage(argv[0]);
                return EXIT_SUCCESS;
            default:
                usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    fd = device_open(device);
    if (fd < 0) {
        return EXIT_FAILURE;
    }

    if (optind < argc) {
        char command[COMMAND_SIZE] = {0};
        for (int i = optind; i < argc; i++) {
            size_t required = strlen(command) + strlen(argv[i]) + (i > optind ? 1U : 0U);
            if (required >= sizeof(command)) {
                fputs("El comando es demasiado largo\n", stderr);
                close(fd);
                return EXIT_FAILURE;
            }
            if (i > optind) {
                strcat(command, " ");
            }
            strcat(command, argv[i]);
        }
        result = run_command(fd, command, timeout_ms);
    } else {
        result = interactive_mode(fd, timeout_ms);
    }

    close(fd);
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#endif
