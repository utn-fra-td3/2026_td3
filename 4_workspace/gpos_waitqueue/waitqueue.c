#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/wait.h>

// Etiqueta para autor del modulo
#define AUTHOR	"utn-fra-td3"

// Puntero para primer hilo
static struct task_struct *thread1;
// Puntero para segundo hilo
static struct task_struct *thread2;

// Cola de espera
static wait_queue_head_t waitqueue;
// Variable comun para condicion
static int condition = 0;

/**
 * @brief Tarea que imprime mensaje en el kernel cada 1 segundo
*/
static int thread1_f(void *params) {
	// Corro hilo si no hay nada que lo haya cerrado
	while (!kthread_should_stop()) {
		// Mensaje en el kernel
		printk("%s: Corriendo thread 1\n", AUTHOR);
		// Demora de un segundo
		msleep(1000);
		// Cambio la condicion
		condition = 2;
		// Despierta los hilos que cumplan con la condicion
		wake_up(&waitqueue);
		// Espero a que se de la condicion
		wait_event_interruptible(waitqueue, condition == 1);
	}
	return 0;
}

/**
 * @brief Tarea que imprime mensaje en el kernel cada 2 segundos
*/
static int thread2_f(void *params) {
	// Corro hilo si no hay nada que lo haya cerrado
	while (!kthread_should_stop()) {
		// Espero a que se de la condicion
		wait_event_interruptible(waitqueue, condition == 2);
		// Mensaje en el kernel
		printk("%s: Corriendo thread 2\n", AUTHOR);
		// Demora de dos segundos
		msleep(2000);
		// Cambio la condicion
		condition = 1;
		// Despierta los hilos que cumplan con la condicion
    wake_up(&waitqueue);
	}
	return 0;
}

/**
 * @brief Llamado cuando se carga el modulo en el kernel
*/
static int __init waitqueue_init(void) {
	// Mensaje en el kernel
	printk("%s: Insertando el modulo de kernel\n", AUTHOR);
	// Inicializo la cola
	init_waitqueue_head(&waitqueue);
	// Inicio condicion para despertar el hilo 1
	condition = 1;
	// Creo y corro el hilo
	thread1 = kthread_run(
		thread1_f,	// Callback
		NULL,				// Sin parametros
		"thread1"		// Nombre del hilo
	);
	// Verifico que se pudo iniciar el hilo 1
	if (IS_ERR(thread1)) {
		printk("%s: Error al crear thread 1\n", AUTHOR);
		return -1;
	}
	// Creo y corro el hilo
	thread2 = kthread_run(
		thread2_f,	// Callback
		NULL,				// Sin parametros
		"thread2"		// Nombre del hilo
	);
	// Verifico que se pudo iniciar el hilo 2
	if (IS_ERR(thread2)) {
		printk("%s: Error al crear thread 2\n", AUTHOR);
		// Elimino el hilo anterior
		kthread_stop(thread1);
		return -1;
	}
	return 0;
}

/**
 * @brief Llamado cuando se quita el modulo del kernel
*/
static void __exit waitqueue_exit(void) {
	// Mensaje en el kernel
	printk("%s: Removiendo el modulo de kernel\n", AUTHOR);
	// Si se pudo crear el hilo
	if (thread1) {
		// Elimino hilo
		kthread_stop(thread1);
	}
	// Si se pudo crear el hilo
	if (thread2) {
		// Elimino hilo
		kthread_stop(thread2);
	}
}

// Registro funciones de inicializacion y salida
module_init(waitqueue_init);
module_exit(waitqueue_exit);

// Informacion del modulo
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);
MODULE_DESCRIPTION("LKM waitqueue test");
