#ifndef __LED_H
#define __LED_H

void LED_Init(void);
void LED_All_OFF(void);
void LED_Control(uint8_t num, uint8_t state);
void LED_PortControl(uint8_t value);

#endif
