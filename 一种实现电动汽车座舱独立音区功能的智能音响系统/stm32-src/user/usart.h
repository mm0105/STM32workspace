#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"

#define USART1_LINE_MAX 64

void USART1_Init(u32 bound);
void USART1_IRQHandler_User(void);
void USART1_ProcessCommand(void);
void USART1_SendString(const char *str);

#endif
