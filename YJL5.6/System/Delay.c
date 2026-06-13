#include "stm32f10x.h"

/**
  * @brief  Microsecond delay
  * @param  xus delay length, range: 0~233015
  * @retval None
  */
void Delay_us(uint32_t xus)
{
	SysTick->LOAD = 72 * xus;				//set reload value
	SysTick->VAL = 0x00;					//clear current value
	SysTick->CTRL = 0x00000005;				//enable SysTick, HCLK source
	while(!(SysTick->CTRL & 0x00010000));	//wait for count to zero
	SysTick->CTRL = 0x00000004;				//disable SysTick
}

/**
  * @brief  Millisecond delay
  * @param  xms delay length, range: 0~4294967295
  * @retval None
  */
void Delay_ms(uint32_t xms)
{
	while(xms--)
	{
		Delay_us(1000);
	}
}

/**
  * @brief  Second delay
  * @param  xs delay length, range: 0~4294967295
  * @retval None
  */
void Delay_s(uint32_t xs)
{
	while(xs--)
	{
		Delay_ms(1000);
	}
}
