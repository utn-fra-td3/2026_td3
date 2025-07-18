#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Prototipos de funciones

void file_write(FILE *file, char *path, char *str);
void file_read(FILE *file, char *path, char *str);

/**
 * @brief Programa principal
 */
int main(int argc, char *argv[]) {
    // Puntero a archivo
	FILE *file;

	while(1) {
        // Tomo de la consola el texto a escribir
		char str[100];
		printf("Ingrese el texto a escribir: ");
		fgets(str, 100, stdin);

		if(!strcmp(str, "exit")) {
			puts("Saliendo del programa...");
			return 0;
		}

        // Escribo y leo el archivo
		file_write(file, "/proc/utn-fra-td3/test", str);
		file_read(file, "/proc/utn-fra-td3/test", str);
        // Muestro lectura por consola
		printf("Contenido: %s\n", str);
	}
}

/**
 * @brief Wrapper para escribir un archivo
 * @param file puntero a archivo
 * @param path ruta al archivo
 * @param str cadena de caracteres para escribir
 */
void file_write(FILE *file, char *path, char *str) {
    // Abro archivo con permisos de escritura, escribo y cierro
	file = fopen(path, "w");
	fputs(str, file);
	fclose(file);
}

/**
 * @brief Wrapper para leer un archivo
 * @param file puntero a archivo
 * @param path ruta al archivo
 * @param str puntero donde escribir el contenido del archivo
 */
void file_read(FILE *file, char *path, char *str) {
    // Abro archivo con permisos de lectura, leo y cierro
	file = fopen(path, "r");
	fgets(str, 100, file);
	fclose(file);
}
