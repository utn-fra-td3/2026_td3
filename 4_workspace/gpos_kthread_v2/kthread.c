#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>

// Etiqueta para el autor del modulo
#define AUTHOR	"utn-fra-td3"

// Mensaje para pasar de parametro a la tarea
#define MSG	"Hola desde una tarea!\n"

// Puntero global a estructura de tarea del kernel
static struct task_struct *g_kernel_task = NULL;

/**
 * @brief Tarea para imprimir un mensaje periodicamente en el kernel
 * @param params puntero a parametros
*/
static int task_handler(void *params) {
	// Corre mientras no haya otros procesos que lo detengan
	while(!kthread_should_stop()) {
		// Mensaje para el kernel
		printk(KERN_INFO "%s: %s", AUTHOR, (char*)params);
		// Demora
		msleep(500);
	}
	return 0;
}

/**
 * @brief Inicializa el modulo cuando se carga en el kernel
 * @return devuelve cero si salio bien
*/
static int __init kernel_thread_init(void) {
	// Creo el hilo para la tarea de hello_kernel
	g_kernel_task = kthread_run(
		task_handler,		// Callback
		(void*)MSG,			// Mensaje que se pasa como parametro
		"Kernel Task"		// Nombre del hilo
	);

	// Verifico si la tarea se pudo crear
	if(g_kernel_task == NULL) {
		// No se pudo crear
		printk(KERN_ERR "%s: No se pudo crear la tarea!\n", AUTHOR);
		return -1;
	}

	return 0;
}

/**
 * @brief Llamado cuando se remueve el modulo del kernel
*/
static void __exit kernel_thread_exit(void) {
	// Si se habia podido crear el hilo
	if(g_kernel_task) {
		// Detengo el hilo
		kthread_stop(g_kernel_task);
	}
}

// Elijo la funcion llamada al cargar y remover el modulo del kernel
module_init(kernel_thread_init);
module_exit(kernel_thread_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("Ejemplo de Kernel Thread");
