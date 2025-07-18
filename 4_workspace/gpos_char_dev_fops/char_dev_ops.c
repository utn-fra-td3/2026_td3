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
static dev_t chrdev_number;
// Variable que representa el char device
static struct cdev chrdev;
// Clase del char device
static struct class *chrdev_class;

// Buffer de datos para compartir entre usuario y kernel
static char shared_buffer[128];

/**
 * @brief Callback llamado cuando se lee del char device
*/
static ssize_t chr_dev_read(struct file *f, char __user *buff, size_t size, loff_t *off) {
    // Variables auxiliares
    int to_copy, not_copied, copied;
    // Si el offset es mayor que el tamaño del buffer, no hay mas datos para leer
    if (*off >= sizeof(shared_buffer)) { return 0; }
    // Reviso cuanto se puede leer
    to_copy = min(size, sizeof(shared_buffer) - *off);
    // Copio lo que hay en el dispositivo de caracteres y lo copio al usuario
    not_copied = copy_to_user(buff, shared_buffer + *off, to_copy);
    // Actualizo el offset
    *off += to_copy;
    // Calculo cuántos bytes se copiaron y devuelvo
    copied = to_copy - not_copied;
    return copied;
}

/**
 * @brief Callback llamado cuando se escribe el char device
*/
static ssize_t chr_dev_write(struct file *f, const char __user *buff, size_t size, loff_t *off) {
	// Variables auxiliares
	int to_copy, not_copied, copied;
	// Reviso si lo que se escribio excede al buffer
	to_copy = min(size, sizeof(shared_buffer));
	// Copio lo escrito al buffer y guardo la cantidad de bytes que no se copiaron
	not_copied = copy_from_user(shared_buffer, buff, to_copy);
	// Calculo cuantos bytes se copiaron
	copied = to_copy - not_copied;
	// Limpio los espacios despues del enter
	for(int i = 0; i < copied; i++) {
		if(shared_buffer[i] == '\n') {
			shared_buffer[i] = 0;
			break;
		}
	}
	// Muestro lo que se escribio en el kernel
	printk(KERN_INFO "%s: Se escribio '%s' en el char device\n", AUTHOR, shared_buffer);
	// Devuelvo la cantidad de bytes copiados
	return copied;
}

// Operaciones de archivos
static struct file_operations chrdev_ops = {
	.owner = THIS_MODULE,
	.read = chr_dev_read,
	.write = chr_dev_write
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
MODULE_DESCRIPTION("Modulo que ");
