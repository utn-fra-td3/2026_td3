#include <linux/module.h>
#include <linux/init.h>

// Etiqueta para el autor del modulo
#define AUTHOR	"utn-fra-td3"

// Nombre del modulo por defecto
static char *drv_name = "default";

/**
 * @brief Funcion llamada cuando se carga el modulo
*/
static int __init param_init(void) {
	printk("%s: Insertando el modulo de kernel\n", AUTHOR);
	printk("%s: drv_name = %s\n", AUTHOR, drv_name);
	return 0;
}

/**
 * @brief Funcion llamada cuando se retira el modulo
*/
static void __exit param_exit(void) {
	pr_info("TD3 - Removiendo el modulo de kernel\n");
}

// Registro funciones de inicializacion y salida
module_init(param_init);
module_exit(param_exit);

// Asigno al momento de compilar el nombre del modulo
module_param(drv_name, charp, S_IRUGO);
MODULE_PARM_DESC(drv_name, "Name of this driver");

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("LKM parameter test");
