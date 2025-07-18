#include "gpio_driver.h"

// Puntero a memoria para manipular el GPIO
static void __iomem *gpio_base = NULL;

/**
 * @brief Solicita la memoria virtual para acceder al GPIO
 * @return devuelve la direccion o NULL si no se pudo
 */
void __iomem* gpio_map(void) {

	if(!gpio_base) {
		// Pide la direccion del GPIO
		gpio_base = ioremap(GPIO_BASE_ADDR, MEM_MAP_SIZE);
	}
	return gpio_base;
}

/**
 * @brief Libera la memoria solicitada para el GPIO
 */
void gpio_unmap(void) {

	if(gpio_base) {
		iounmap(gpio_base);
		gpio_base = NULL;
	}
}

/**
 * @brief Obtiene la direccion del GPFSELn
 * @param gpio numero de GPIO
 * @return direccion del GPFSELn
 */
void __iomem* gpio_get_fsel(unsigned int gpio) {
	// Los GPIO se agrupan de a 10 por registro
	int fsel_index = gpio / 10;
	return (gpio_base + 4 * fsel_index);
}

/**
 * @brief Obtiene el desplazamiento dentro de un registro GPFSELn
 * @param gpio numero de GPIO
 * @return desplazamiento dentro del GPFSELn
 */
unsigned int gpio_get_fsel_shift(unsigned int gpio) {
	// Los GPIO se agrupan de a 10 por registro y ocupan 3 bits
	return 3 * (gpio % 10);
}

/**
 * @brief Configura como salida el GPIO elegido
 * @param gpio numero de GPIO
 */
void gpio_set_dir_output(unsigned int gpio) {
	// Obtiene el registro a modificar y desplazamiento
	void __iomem *gpio_fsel = gpio_get_fsel(gpio);
	unsigned int fsel_shift = gpio_get_fsel_shift(gpio);
	// Limpia funciones anteriores
	unsigned int reg_val = ioread32(gpio_fsel);
	reg_val &= ~(0b111 << fsel_shift);
	// Configura como salida
	reg_val |= (GPIO_FUNC_OUTPUT << fsel_shift);
	iowrite32(reg_val, gpio_fsel);}

/**
 * @brief Hace un set en el GPIO elegido
 * @param gpio numero de GPIO
 */
void gpio_set(unsigned int gpio) {
	if(!gpio_base) return;
  // Obtiene el registro que tiene que modificar
  unsigned int shift = (gpio < 32)? GPSET0_OFF : GPSET1_OFF;
  void __iomem *gpset = gpio_base + shift;
	iowrite32(1 << (gpio % 32), gpset);
}

/**
 * @brief Hace un clear en el GPIO elegido
 * @param gpio numero de GPIO
 */
void gpio_clr(unsigned int gpio) {
	if(!gpio_base) return;
  // Obtiene el registro que tiene que modificar
  unsigned int shift = (gpio < 32)? GPCLR0_OFF : GPCLR1_OFF;
  void __iomem *gpclr = gpio_base + shift;
	iowrite32(1 << (gpio % 32), gpclr);
}
