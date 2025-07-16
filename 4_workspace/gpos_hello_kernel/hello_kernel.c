#include <linux/module.h>
#include <linux/init.h>

// Etiqueta para el autor del modulo
#define AUTHOR	"utn-fra-td3"

/**
 * @brief Se llama cuando el modulo se carga en el kernel
 * @return devuelve cero cuando la funcion termina con exito
*/
static int __init hello_kernel_init(void) {
	printk("%s: Hola Kernel!\n", AUTHOR);
	return 0;
}

/**
 * @brief Se llama cuando el modulo se quita del kernel
 */
static void __exit hello_kernel_exit(void) {
	printk("%s: Chau Kernel!\n", AUTHOR);
}

// Registro la funcion de inicializacion y salida
module_init(hello_kernel_init);
module_exit(hello_kernel_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Hola mundo version kernel");
