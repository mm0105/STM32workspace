#ifndef __KEY_H
#define __KEY_H

#include "stm32f10x.h"

/* Key pin definitions (matches dev board hardware) */
#define KEY1_PORT   GPIOA
#define KEY1_PIN    GPIO_Pin_15  /* KEY1: PA15 */
#define KEY2_PORT   GPIOA
#define KEY2_PIN    GPIO_Pin_14  /* KEY2: PA14 */
#define KEY3_PORT   GPIOA
#define KEY3_PIN    GPIO_Pin_13  /* KEY3: PA13 */
#define KEY4_PORT   GPIOA
#define KEY4_PIN    GPIO_Pin_12  /* KEY4: PA12 */

/* Key return values */
#define KEY_NONE    0
#define KEY1_PRESS  1
#define KEY2_PRESS  2
#define KEY3_PRESS  3
#define KEY4_PRESS  4

void Key_Init(void);
uint8_t Key_Scan(void);         /* non-blocking scan, returns key number */
uint8_t Key_IsPressed(uint8_t key_num); /* check if key pressed (raw level, no debounce) */

#endif

