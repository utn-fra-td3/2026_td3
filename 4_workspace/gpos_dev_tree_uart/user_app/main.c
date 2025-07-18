#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

// Nombre de archivo
#define CDEV_NAME	"/dev/td3_uart"
// Maximo largo
#define MAX_LEN		64

/**
 * @brief Programa principal
*/
int main(void) {
	// Variable para buffer
	char str[MAX_LEN];
	// Variables para el manejo de archivo
	int dev;
	// Abro archivo como lectura escritura
	dev = open(CDEV_NAME, O_RDWR);
	// Verifico que se haya podido abrir
	if(dev == -1) {
		puts("No se pudo abrir el archivo\n");
		return -1;
	}
	// Mensaje por consola
	puts("Escriba lo que quiere ver por UART: ");

	while(1) {
		// Guardo el dato para escribir
		fgets(str, MAX_LEN, stdin);
		// Escrivo al device
		write(dev, str, MAX_LEN);
		// Leo el archivo
		read(dev, str, sizeof(str));
		// Muestro lo que fue escrito
		printf("Leido del device: %s\n", str);
	}
	return 0;
}
