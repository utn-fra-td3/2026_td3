#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include <stdio.h>

#define ENCODER_PIN_A       4   
#define ENCODER_PIN_B       5
#define ENCODER_LOW_LIMIT  -100
#define ENCODER_HIGH_LIMIT 4200 



static void encoder_init_pcnt(void);