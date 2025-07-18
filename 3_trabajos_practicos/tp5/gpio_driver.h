#include <linux/io.h>

/**
 * @brief Direccion base de perifericos
 * @see BCM2711 ARM Peripherals, Seccion 1.2, https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 */
#define MAIN_PERIPHERALS_ADDR 0xFE000000
/**
 * @brief Desplazamiento de direccion de GPIO
 * @see BCM2711 ARM Peripherals, Seccion 5.2, https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 */
#define GPIO_ADDR_SHIFT 0x200000
/**
 * @brief Direccion base de GPIO
 * @see BCM2711 ARM Peripherals, Seccion 5.2, https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 */
#define GPIO_BASE_ADDR	(MAIN_PERIPHERALS_ADDR + GPIO_ADDR_SHIFT)
/**
 * @brief Cantidad de espacio que se va a pedir
 * @see BCM2711 ARM Peripherals, Seccion 5.2, https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 */
#define MEM_MAP_SIZE	(4 * 1024)
/**
 * @brief Funcion de GPIO como salida
 * @see BCM2711 ARM Peripherals, Seccion 5.2: GPFSELn Register, https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 */
#define GPIO_FUNC_OUTPUT	0x01
/**
 * @brief Desplazamiento de registro GPSET0
 * @see BCM2711 ARM Peripherals, Seccion 5.2, https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 */
#define GPSET0_OFF	0x1c
/**
 * @brief Desplazamiento de registro GPSET1
 * @see BCM2711 ARM Peripherals, Seccion 5.2, https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 */
#define GPSET1_OFF	0x20
/**
 * @brief Desplazamiento de registro GPCLR0
 * @see BCM2711 ARM Peripherals, Seccion 5.2, https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 */
#define GPCLR0_OFF	0x28
/**
 * @brief Desplazamiento de registro GPCLR1
 * @see BCM2711 ARM Peripherals, Seccion 5.2, https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf
 */
#define GPCLR1_OFF	0x2c

// Prototipos

void __iomem* gpio_map(void);
void gpio_unmap(void);
void __iomem* gpio_get_fsel(unsigned int gpio);
unsigned int gpio_get_fsel_shift(unsigned int gpio);
void gpio_set_dir_output(unsigned int gpio);
void gpio_set(unsigned int gpio);
void gpio_clr(unsigned int gpio);
