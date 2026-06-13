#include "stm32f10x.h"                  // Device header

/**
  * 函    数：LED初始化
  * 参    数：无
  * 返 回 值：无
  */
void LED_Init(void)
{
	/*开启时钟*/
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);		//开启GPIOA的时钟
	
	/*GPIO初始化*/
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | 
										GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);						//将PA0-PA7引脚初始化为推挽输出
	
	/*设置GPIO初始化后的默认电平*/
	GPIO_SetBits(GPIOA, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | 
					GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7);	//设置PA0-PA7引脚为高电平
}

/**
  * 函    数：LED全灭
  * 参    数：无
  * 返 回 值：无
  */
void LED_All_OFF(void)
{
	GPIO_SetBits(GPIOA, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | 
					GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7);	//设置PA0-PA7引脚为高电平
}

/**
  * 函    数：控制单个LED
  * 参    数：LED编号(1-8)，状态(0:关闭, 1:开启)
  * 返 回 值：无
  */
void LED_Control(uint8_t num, uint8_t state)
{
	if (num >= 1 && num <= 8)
	{
		if (state)
		{
			GPIO_ResetBits(GPIOA, GPIO_Pin_0 << (num - 1));	//设置对应引脚为低电平
		}
		else
		{
			GPIO_SetBits(GPIOA, GPIO_Pin_0 << (num - 1));	//设置对应引脚为高电平
		}
	}
}

/**
  * 函    数：使用端口操作控制LED
  * 参    数：控制值(低8位对应PA0-PA7)
  * 返 回 值：无
  */
void LED_PortControl(uint8_t value)
{
	GPIO_Write(GPIOA, (GPIO_ReadOutputData(GPIOA) & 0xFF00) | (~value & 0xFF));
}

