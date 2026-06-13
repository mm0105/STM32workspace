#include "Key.h"
#include "Delay.h"

/**
 * 按键初始化
 * - 配置 PA12~PA15 为上拉输入
 * - 禁用 JTAG 和 SWD, 释放 PA13/PA14/PA15 作为普通 GPIO
 */
void Key_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = KEY1_PIN | KEY2_PIN | KEY3_PIN | KEY4_PIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/* 禁用 JTAG 和 SWD, 释放 PA13/PA14/PA15 */
	GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
	GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);
}

/**
 * 非阻塞按键扫描
 * - 单次按下检测(必须抬手后才允许再次触发)
 * - 返回值: KEY1_PRESS(1) ~ KEY4_PRESS(4), KEY_NONE(0)
 * - 消抖时间: 20ms
 */
uint8_t Key_Scan(void)
{
	static uint8_t key1_released = 1;
	static uint8_t key2_released = 1;
	static uint8_t key3_released = 1;
	static uint8_t key4_released = 1;

	/* KEY1: PA15 */
	if (key1_released == 0)
	{
		if (GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN) == 1)
			key1_released = 1;
	}
	else if (GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN) == 0)
	{
		Delay_ms(20);
		if (GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN) == 0)
		{
			key1_released = 0;
			return KEY1_PRESS;
		}
	}

	/* KEY2: PA14 */
	if (key2_released == 0)
	{
		if (GPIO_ReadInputDataBit(KEY2_PORT, KEY2_PIN) == 1)
			key2_released = 1;
	}
	else if (GPIO_ReadInputDataBit(KEY2_PORT, KEY2_PIN) == 0)
	{
		Delay_ms(20);
		if (GPIO_ReadInputDataBit(KEY2_PORT, KEY2_PIN) == 0)
		{
			key2_released = 0;
			return KEY2_PRESS;
		}
	}

	/* KEY3: PA13 */
	if (key3_released == 0)
	{
		if (GPIO_ReadInputDataBit(KEY3_PORT, KEY3_PIN) == 1)
			key3_released = 1;
	}
	else if (GPIO_ReadInputDataBit(KEY3_PORT, KEY3_PIN) == 0)
	{
		Delay_ms(20);
		if (GPIO_ReadInputDataBit(KEY3_PORT, KEY3_PIN) == 0)
		{
			key3_released = 0;
			return KEY3_PRESS;
		}
	}

	/* KEY4: PA12 */
	if (key4_released == 0)
	{
		if (GPIO_ReadInputDataBit(KEY4_PORT, KEY4_PIN) == 1)
			key4_released = 1;
	}
	else if (GPIO_ReadInputDataBit(KEY4_PORT, KEY4_PIN) == 0)
	{
		Delay_ms(20);
		if (GPIO_ReadInputDataBit(KEY4_PORT, KEY4_PIN) == 0)
		{
			key4_released = 0;
			return KEY4_PRESS;
		}
	}

	return KEY_NONE;
}

/**
 * 检测按键是否按下(读取原始电平, 无消抖)
 * key_num: 按键编号 (1~4)
 * 返回: 1 = 按下(低电平), 0 = 松开
 */
uint8_t Key_IsPressed(uint8_t key_num)
{
	switch (key_num)
	{
		case 1:
			return (GPIO_ReadInputDataBit(KEY1_PORT, KEY1_PIN) == 0);
		case 2:
			return (GPIO_ReadInputDataBit(KEY2_PORT, KEY2_PIN) == 0);
		case 3:
			return (GPIO_ReadInputDataBit(KEY3_PORT, KEY3_PIN) == 0);
		case 4:
			return (GPIO_ReadInputDataBit(KEY4_PORT, KEY4_PIN) == 0);
		default:
			return 0;
	}
}
