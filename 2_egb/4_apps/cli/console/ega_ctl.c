#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

#define DEVICE_PATH "/dev/egalink"
#define MAX_COMMAND 256
#define MAX_RESPONSE 512
#define RESPONSE_TIMEOUT_MS 2000

static int send_command(int fd, const char *command)
{
    char tx[MAX_COMMAND + 2];
    char response[MAX_RESPONSE];
    size_t command_len;
    size_t rx_len = 0;

    command_len = strlen(command);

    if (command_len == 0 || command_len >= MAX_COMMAND) {
        fprintf(stderr, "Comando invalido o demasiado largo\n");
        return -1;
    }

    memcpy(tx, command, command_len);
    tx[command_len++] = '\n';

    if (write(fd, tx, command_len) != (ssize_t)command_len) {
        perror("write");
        return -1;
    }

    while (rx_len < sizeof(response) - 1) {
        struct pollfd pfd = {
            .fd = fd,
            .events = POLLIN
        };

        int ret = poll(&pfd, 1, RESPONSE_TIMEOUT_MS);

        if (ret < 0) {
            perror("poll");
            return -1;
        }

        if (ret == 0) {
            fprintf(stderr, "Timeout esperando respuesta de la EGA\n");
            return -1;
        }

        char ch;

        ret = read(fd, &ch, 1);

        if (ret < 0) {
            perror("read");
            return -1;
        }

        if (ret == 0)
            continue;

        if (ch == '\r')
            continue;

        if (ch == '\n')
            break;

        response[rx_len++] = ch;
    }

    response[rx_len] = '\0';

    printf("%s\n", response);

    return 0;
}

int main(void)
{
    int fd;
    char command[MAX_COMMAND];

    fd = open(DEVICE_PATH, O_RDWR);

    if (fd < 0) {
        perror("No se pudo abrir " DEVICE_PATH);
        return EXIT_FAILURE;
    }

    printf("EGA Control - TD3\n");
    printf("Dispositivo: %s\n", DEVICE_PATH);
    printf("Escriba 'exit' para salir.\n\n");

    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL)
            break;

        command[strcspn(command, "\r\n")] = '\0';

        if (strcmp(command, "exit") == 0 ||
            strcmp(command, "quit") == 0) {
            break;
        }

        if (command[0] == '\0')
            continue;

        send_command(fd, command);
    }

    close(fd);

    return EXIT_SUCCESS;
}
