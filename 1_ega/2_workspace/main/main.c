#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "config.h"
#include "uart_comm.h"
#include "teclado.h"
#include "flash.h"
#include "lcd.h"
#include "trigger.h"
#include "medyparam.h"
#include "adc.h"
#include "digipot.h"

void app_main(void)
{
    flash_init();        
    config_init();       
    uart_comm_init();    
    teclado_init();      
    lcd_init_task();     
    trigger_init();      
    adc_init();      
    digipot_init();     
    medyparam_init();    
    flash_task_start();  
}
