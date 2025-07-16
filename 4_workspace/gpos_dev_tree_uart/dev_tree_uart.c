#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/serdev.h>
#include <linux/fs.h>
#include <linux/of_device.h>
#include <linux/uaccess.h>

// Autor del modulo
#define AUTHOR		"utn-fra-td3"
// Char device name
#define CDEV_NAME	"td3_uart"
// Minor number del char device
#define CDEV_MINOR	50
// Cantidad de devices para reservar
#define CDEV_COUNT	1
// Cantidad maxima de bytes para el buffer de usuario
#define SHARED_BUFF_SIZE	64

// IDs de serial devices
static struct of_device_id serdev_ids[] = {
	{ .compatible = "brightlight,td3_uart", },
	{ }
};
MODULE_DEVICE_TABLE(of, serdev_ids);

// Puntero global para UART
static struct serdev_device *g_serdev = NULL;

/**
 * @brief Llamada cuando se recibe mensaje por UART
*/
static int td3_uart_recv(struct serdev_device *serdev, const unsigned char *buffer, size_t size) {
	// Puntero a cadena
	static char str[SHARED_BUFF_SIZE] = {0};
	static int i = 0;
	// Veo si es un caracter valido
	if(*buffer) {
		// Copio el caracter
		str[i++] = *buffer;
	}
	// Verifico si me excedi
	if(i == SHARED_BUFF_SIZE || str[i-1] == '\0') {
		// Imprimo el mensaje recibido
		printk(KERN_INFO "%s: Se recibieron %d bytes por UART. El mensaje fue '%s'\n", AUTHOR, i-1, str);
		// Reinicio las variables
		memset(str, 0, i);
		i = 0;
	}
	return size;
}

// Estructura de operaciones para UART
static const struct serdev_device_ops td3_uart_ops = {
	.receive_buf = td3_uart_recv,
};

/**
 * @brief Se llama cuando se conecta un dispositivo UART que matchea con el
 * descrito en la tabla de IDs
*/
static int td3_uart_probe(struct serdev_device *serdev) {
	printk(KERN_INFO "%s: Se conecto un dispositivo UART\n", AUTHOR);
	// Registro las operaciones
	serdev_device_set_client_ops(serdev, &td3_uart_ops);
	// Intento abrir el puerto UART
	if(serdev_device_open(serdev)) {
		printk(KERN_ERR "%s: Error abriendo el puerto UART\n", AUTHOR);
		return -1;
	}

	// Configuro UART
	serdev_device_set_baudrate(serdev, 115200);
	serdev_device_set_flow_control(serdev, false);
	serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);
	// Guardo el puntero
	g_serdev = serdev;
	if(g_serdev == NULL) {
		printk(KERN_ERR "%s: Algo salio mal con el puerto UART\n", AUTHOR);
		return -1;
	}
	return 0;
}

/**
 * @brief Se llama cuando se desconecta el dispositivo UART
*/
static void td3_uart_remove(struct serdev_device *serdev) {
	printk(KERN_INFO "%s: Cerrando UART\n", AUTHOR);
	// Cierro el dispositivo
	serdev_device_close(serdev);
}


// Estructura de implementacion del driver
static struct serdev_device_driver td3_uart_driver = {
	.probe = td3_uart_probe,
	.remove = td3_uart_remove,
	.driver = {
		.name = "td3_uart_driver",
		.of_match_table = serdev_ids,
	},
};

// Estructura para manejar el char device
typedef struct {
	struct cdev cdev;			// Guarda el char device
	dev_t cdev_number;			// Guarda el major y minor number
	unsigned int cdev_major;	// Numero mayor
	struct class *cdev_class;	// Clase del char device
} td3_cdev_t;

// Variable para mi char device
td3_cdev_t td3_cdev;

// Buffer compartido entre usuario y kernel
char shared_buffer[SHARED_BUFF_SIZE];

/**
 * @brief Se llama cuando se lee el archivo
*/
static ssize_t cdev_echo_read(struct file *f, char __user *buff, size_t size, loff_t *off) {
	// Variables para cantidad de bytes escritos
	int not_copied, to_copy = (size > SHARED_BUFF_SIZE)? SHARED_BUFF_SIZE : size;
	// Veo si se puede copiar
	if(*off >= to_copy) { return 0; }
	// Copio al user
	not_copied = copy_to_user(shared_buffer, buff, to_copy);
	// Mensaje testigo para el kernel
	printk("%s: Leido sobre /dev/%s - %s\n", AUTHOR, CDEV_NAME, shared_buffer);
	*off = to_copy - not_copied;
	// Devuelvo lo que falta
	return to_copy - not_copied;
}

/**
 * @brief Se llama cuando se escribe el archivo
*/
static ssize_t cdev_echo_write(struct file *f, const char __user *buff, size_t size, loff_t *off) {
	// Variables para cantidad de bytes escritos
	int not_copied, to_copy = (size > SHARED_BUFF_SIZE)? SHARED_BUFF_SIZE : size;
	// Copio del user para escribir
	not_copied = copy_from_user(shared_buffer, buff, to_copy);
	// Mensaje testigo para el kernel
	printk("%s: Escrito sobre /dev/%s - %s\n", AUTHOR, CDEV_NAME, shared_buffer);
	// Mando el mensaje por UART
	if(g_serdev != NULL) {
		serdev_device_write_buf(g_serdev, shared_buffer, sizeof(shared_buffer));
		// Devuelvo la cantidad de bytes que se copiaron
		return to_copy - not_copied;
	}
	return 0;
}

// Estructura para implementacion de operaciones con archivos
const struct file_operations fops = {
	.owner = THIS_MODULE,
	.read = cdev_echo_read,
	.write = cdev_echo_write
};

/**
 * @brief Se llama cuando se carga el modulo en el kernel
 * @return devuelve cero si salio bien
*/
static int __init td3_uart_init(void) {
	// Intento crear el char device
	if(alloc_chrdev_region(&td3_cdev.cdev_number, CDEV_MINOR, CDEV_COUNT, CDEV_NAME) < 0) {
		printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
		return -1;
	}
	// Muestro informacion del chdev
	printk(KERN_INFO "%s: Char device creado en major %d, minor %d\n", AUTHOR, MAJOR(td3_cdev.cdev_number), MINOR(td3_cdev.cdev_number));
	// Registro las operaciones al char device
	cdev_init(&td3_cdev.cdev, &fops);
	td3_cdev.cdev.owner = THIS_MODULE;
	// Lo asocio con el char device
	if(cdev_add(&td3_cdev.cdev, td3_cdev.cdev_number, CDEV_COUNT) < 0) {
		// Error
		unregister_chrdev_region(td3_cdev.cdev_number, CDEV_COUNT);
		printk(KERN_ERR "%s: No se pudo crear el char device\n", AUTHOR);
		return -1;
	}
	// Creo estructura de clase
	td3_cdev.cdev_class = class_create(CDEV_NAME);
	// Verifico si fue posible
	if(IS_ERR(td3_cdev.cdev_class)) {
		// Error
		printk(KERN_ERR "%s: No se pudo crear la clase del char device\n", AUTHOR);
		class_destroy(td3_cdev.cdev_class);
		unregister_chrdev_region(td3_cdev.cdev_number, CDEV_COUNT);
		return -1;
	}
	// Creo el archivo del char device
	if(IS_ERR(device_create(td3_cdev.cdev_class, NULL, td3_cdev.cdev_number, NULL, CDEV_NAME))) {
		// Error
		printk(KERN_ERR "%s: No se pudo crear el archivo del char device\n", AUTHOR);
		class_destroy(td3_cdev.cdev_class);
		unregister_chrdev_region(td3_cdev.cdev_number, CDEV_COUNT);
		return -1;
	}
	printk(KERN_INFO "%s: Archivo de char device creado!\n", AUTHOR);

	// Intento registrar el driver para el UART
	if(serdev_device_driver_register(&td3_uart_driver)) {
		printk(KERN_ERR "%s: No se pudo crear el driver de UART\n", AUTHOR);
		return -1;
	}
	printk(KERN_INFO "%s: Driver para UART registrado!\n", AUTHOR);
	return 0;
}

/**
 * @brief Se llama cuando se remueve el modulo del kernel
*/
static void __exit td3_uart_exit(void) {
	// Mensaje para el kernel
	printk("%s: Driver deshabilitado\n", AUTHOR);
	// Elimino el device
	device_destroy(td3_cdev.cdev_class, td3_cdev.cdev_number);
	// Elimino la clase
	class_destroy(td3_cdev.cdev_class);
	// Libero la region
	unregister_chrdev_region(td3_cdev.cdev_number, CDEV_COUNT);
	// Elimino el char device
	cdev_del(&td3_cdev.cdev);
	// Elimino el driver de UART
	serdev_device_driver_unregister(&td3_uart_driver);
}

// Registro funciones de inicializacion y saiida del driver
module_init(td3_uart_init);
module_exit(td3_uart_exit);

/* Meta Information */
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Modulo que inicializa y registra un char device");
