#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/fs.h>

// Etiqueta para el autor del modulo
#define AUTHOR	"utn-fra-td3"

// Minor number del device
#define CHRDEV_MINOR	50
// Cantidad de devices para reservar
#define CHRDEV_COUNT	1

// Variable que guarda los major y minor numbers del char device
dev_t chrdev_number;
// Variable que representa el char device
struct cdev chrdev;
// Clase del char device
struct class *chrdev_class;

// Operaciones de archivos
static struct file_operations chrdev_ops = {
	.owner = THIS_MODULE
};

/**
 * @brief Se llama cuando el modulo se carga en el kernel
 * @return devuelve cero cuando la funcion termina con exito
*/
static int __init chrdev_init(void) {
	// Intento ver de reservar el char device
	if(alloc_chrdev_region(&chrdev_number, CHRDEV_MINOR, CHRDEV_COUNT, AUTHOR) < 0) {
		printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
		return -1;
	}
	// Mensaje informativo para ver el major y minor number
	printk(KERN_INFO
		"%s: Reservada una region para un char device con major %d y minor %d\n",
		AUTHOR,
		MAJOR(chrdev_number),
		MINOR(chrdev_number)
	);

	// Inicializo el char device con sus operaciones
	cdev_init(&chrdev, &chrdev_ops);
	// Asocio el char device con la region reservada
	if(cdev_add(&chrdev, chrdev_number, CHRDEV_COUNT) < 0) {
		unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
		printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
		return -1;
	}

	// Creo estructura de clase
	chrdev_class = class_create(AUTHOR);
	// Verifico si fue posible
	if(IS_ERR(chrdev_class)) {
		unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
		printk(KERN_ERR "%s: No se pudo crear la clase del char device\n", AUTHOR);
		return -1;
	}

	// Creo el archivo del char device
	if(IS_ERR(device_create(chrdev_class, NULL, chrdev_number, NULL, AUTHOR))) {
		class_destroy(chrdev_class);
		unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
		printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
		return -1;
	}

	printk(KERN_INFO "%s: Fue creado el char device\n", AUTHOR);
	return 0;
}

/**
 * @brief Se llama cuando el modulo se quita del kernel
 */
static void __exit chrdev_exit(void) {
	// Libero la zona del char device
	device_destroy(chrdev_class, chrdev_number);
	class_destroy(chrdev_class);
	unregister_chrdev_region(chrdev_number, CHRDEV_COUNT);
	cdev_del(&chrdev);
}

// Registro la funcion de inicializacion y salida
module_init(chrdev_init);
module_exit(chrdev_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Modulo que crea un char device");
