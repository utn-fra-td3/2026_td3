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

static bool valid_command(const char *command) {
    char setting[4];
    char branch;
    char state[4];
    float value;
    int consumed = 0;

    if (strcmp(command, "GET") == 0 || strcmp(command, "SAVE") == 0) {
        return true;
    }
    if (sscanf(command, "RESET %c %n", &branch, &consumed) == 1 &&
        command[consumed] == '\0') {
        return branch == 'P' || branch == 'N';
    }
    consumed = 0;
    if (sscanf(command, "SET %3s %f %n", setting, &value, &consumed) == 2 &&
        command[consumed] == '\0') {
        if (strcmp(setting, "P") == 0 || strcmp(setting, "N") == 0) {
            return value >= 0.0f && value <= 30.0f;
        }
        if (strcmp(setting, "ILP") == 0 || strcmp(setting, "ILN") == 0) {
            return value >= 0.10f && value <= 0.75f;
        }
        if (strcmp(setting, "VLP") == 0 || strcmp(setting, "VLN") == 0) {
            return value >= 5.0f && value <= 40.0f;
        }
        return false;
    }
    consumed = 0;
    if (sscanf(command, "OUT %c %3s %n", &branch, state, &consumed) == 2 &&
        command[consumed] == '\0') {
        return (branch == 'P' || branch == 'N') &&
               (strcmp(state, "ON") == 0 || strcmp(state, "OFF") == 0);
    }
    return false;
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

static int exchange_command(int fd, const char *command, int timeout_ms,
                            char *response, size_t response_size) {
    struct pollfd descriptor = {.fd = fd, .events = POLLIN};
    size_t used = 0U;

    if (write_all(fd, command, strlen(command)) != 0 || write_all(fd, "\n", 1U) != 0) {
        fprintf(stderr, "Error enviando el comando: %s\n", strerror(errno));
        return -1;
    }
    while (used + 1U < response_size) {
        int ready = poll(&descriptor, 1, timeout_ms);
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
    if (!valid_command(command)) {
        fprintf(stderr, "Comando invalido: %s\nEscriba HELP para ver la lista.\n", command);
        return -1;
    }
    if (exchange_command(fd, command, timeout_ms, response, sizeof(response)) != 0) {
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
