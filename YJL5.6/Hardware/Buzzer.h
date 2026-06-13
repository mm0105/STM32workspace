#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f10x.h"

void Buzzer_Init(void);
void Buzzer_Beep(uint16_t duration_ms);
void Buzzer_Off(void);

#endif
