#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

// Etiqueta para autor del modulo
#define AUTHOR			"utn-fra-td3"
// Etiqueta para el nombre del directorio
#define FOLDER_NAME		AUTHOR
// Etiqueta para el nombde del archivo
#define FILE_NAME		"test"

// Puntero a directorio
static struct proc_dir_entry *folder;
// Puntero a archivo
static struct proc_dir_entry *file;

// Buffer de datos para compartir entre usuario y kernel
static char shared_buffer[128];

/**
 * @brief Callback llamado cuando se lee del char device
*/
static ssize_t _read(struct file *f, char __user *buff, size_t size, loff_t *off) {
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
static ssize_t _write(struct file *f, const char __user *buff, size_t size, loff_t *off) {
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
	printk(KERN_INFO "%s: Se escribio '%s' en el proc file\n", AUTHOR, shared_buffer);
	// Devuelvo la cantidad de bytes copiados
	return copied;
}

// Operaciones para el archivo
static struct proc_ops fops = {
	.proc_read = _read,		// Callback llamado cuando se lee el archivo
	.proc_write = _write	// Callback llamado cuando se escribe el archivo
};

/**
 * @brief Llamado cuando se carga el modulo en el kernel
*/
static int __init procfs_init(void) {
	// Mensaje para el modulo
	printk("%s: Insertando el modulo de kernel\n", AUTHOR);
	// Intento crear el directorio
	if(NULL == (folder = proc_mkdir(FOLDER_NAME, NULL))) {
		printk("%s: Error al crear la carpeta /proc/%s\n", AUTHOR, FOLDER_NAME);
		return -ENOMEM;
	}
	// Intento crear el archivo y registrar sus operaciones
	if(NULL == (file = proc_create(FILE_NAME, 0666, folder, &fops))) {
		printk("%s: Error al crear el archivo /proc/%s/%s\n", AUTHOR, FOLDER_NAME, FILE_NAME);
		// Elimino el directorio
		proc_remove(folder);
		return -1;
	}
	return 0;
}

/**
 * @brief Llamado cuando se retira el modulo del kernel
*/
static void __exit procfs_exit(void) {
	// Mensaje para el kernel
	printk("%s: Removiendo el modulo de kernel\n", AUTHOR);
	// Elimino archivo y directorio
	proc_remove(file);
	proc_remove(folder);
}

// Registro inicializacion y salida del modulo
module_init(procfs_init);
module_exit(procfs_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR("julianvroey - TD3 - UTN FRA");
MODULE_DESCRIPTION("LKM procfs test");
