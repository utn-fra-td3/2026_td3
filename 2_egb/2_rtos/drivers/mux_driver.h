#ifndef MUX_DRIVER_H
#define MUX_DRIVER_H
 
typedef enum {
    MUX_CH_SINE     = 0,  // A=0 B=0 → Y0 → pin 13
    MUX_CH_TRIANGLE = 1,  // A=1 B=0 → Y1 → pin 14
    MUX_CH_SQUARE   = 2,  // A=0 B=1 → Y2 → pin 15
} mux_channel_t;
 
// Inicializa los GPIOs del mux
void mux_init(void);
 
// Selecciona el canal activo
void mux_select(mux_channel_t ch);
 
#endif // MUX_DRIVER_H
 