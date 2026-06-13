#include "Buzzer.h"

/**
 * 蜂鸣器初始化
 * - 配置 PB0 为复用推挽输出(TIM3_CH3)
 * - TIM3: 72MHz / 72 / 100 = 10KHz PWM
 * - 初始占空比 0(关闭)
 */
void Buzzer_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	/* PB0 复用推挽输出 */
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Speed  = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	/* TIM3 PWM 配置 */
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	TIM_TimeBaseStructure.TIM_Period        = 100 - 1;
	TIM_TimeBaseStructure.TIM_Prescaler     = 72 - 1;
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);

	TIM_OCInitTypeDef TIM_OCInitStructure;
	TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_Pulse      = 0;   /* 初始关闭 */
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OC3Init(TIM3, &TIM_OCInitStructure);
	TIM_OC3PreloadConfig(TIM3, TIM_OCPreload_Enable);

	TIM_ARRPreloadConfig(TIM3, ENABLE);
	TIM_Cmd(TIM3, ENABLE);
}

/**
 * 蜂鸣器鸣叫指定毫秒数
 * ms: 鸣叫时长(毫秒)
 * - 频率: 10KHz PWM, 占空比 50%
 * - 阻塞延时
 */
void Buzzer_Beep(uint16_t ms)
{
	TIM_SetCompare3(TIM3, 50);  /* 占空比 50% */
	Delay_ms(ms);
	TIM_SetCompare3(TIM3, 0);   /* 关闭 */
}

/**
 * 蜂鸣器关闭
 */
void Buzzer_Off(void)
{
	TIM_SetCompare3(TIM3, 0);
}
