#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/delay.h>

// Etiqueta para el autor del modulo
#define AUTHOR	"utn-fra-td3"

// Punteros a estructuras de tareas
static struct task_struct *g_kernel_hello_task = NULL;
static struct task_struct *g_kernel_bye_task = NULL;
// Estructura de mutex
static struct mutex g_mutex;

/**
 * @brief Tarea para imprimir un mensaje periodicamente en el kernel
 * @param params puntero a parametros
*/
static int task_handler(void *params) {
	// Corre mientras no haya otros procesos que lo detengan
	while(!kthread_should_stop()) {
		// Intenta tomar el mutex
		mutex_lock(&g_mutex);
		// Mensaje para el kernel
		printk(KERN_INFO "%s: %s", AUTHOR, (char*)params);
		// Libera el mutex
		mutex_unlock(&g_mutex);
		// Demora
		msleep(1000);
	}
	return 0;
}

/**
 * @brief Inicializa el modulo cuando se carga en el kernel
 * @return devuelve cero si salio bien
*/
static int __init kernel_threads_init(void) {
	// Creo el hilo para la tarea
	g_kernel_hello_task = kthread_run(
		task_handler,								// Callback
		(void*)"Hola desde tarea",	// Mensaje que se pasa como parametro
		"Kernel Hello Task"					// Nombre del hilo
	);

	// Creo el hilo para la tarea
	g_kernel_bye_task = kthread_run(
		task_handler,								// Callback
		(void*)"Chau desde tarea",	// Mensaje que se pasa como parametro
		"Kernel Bye Task"						// Nombre del hilo
	);

	// Verifico si la tarea se pudo crear
	if(IS_ERR(g_kernel_hello_task) || IS_ERR(g_kernel_bye_task)) {
		// No se pudo crear
		printk(KERN_ERR "%s: No se pudo crear la tarea!\n", AUTHOR);
		return -1;
	}

	// Inicializo el mutex
	mutex_init(&g_mutex);

	return 0;
}

/**
 * @brief Llamado cuando se remueve el modulo del kernel
*/
static void __exit kernel_threads_exit(void) {
	// Si se habia podido crear el hilo
	if(g_kernel_hello_task) {
		// Detengo el hilo
		kthread_stop(g_kernel_hello_task);
	}

	if(g_kernel_bye_task) {
		// Detengo el hilo
		kthread_stop(g_kernel_bye_task);
	}
}

// Elijo la funcion llamada al cargar y remover el modulo del kernel
module_init(kernel_threads_init);
module_exit(kernel_threads_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Ejemplo de mutex en el Kernel");
