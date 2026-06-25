#include "stm32f10x.h"
#include "misc.h"
#include "stepper.h"
#include "usart.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    USART1_Init(9600);
    Stepper_Init();

    USART1_SendString("STM32 STEP READY\r\n");

    while (1)
    {
        USART1_ProcessCommand();
    }
}
