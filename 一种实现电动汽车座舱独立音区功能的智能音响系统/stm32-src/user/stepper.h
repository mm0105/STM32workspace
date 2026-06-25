#ifndef __STEPPER_H
#define __STEPPER_H

#include "stm32f10x.h"

#define STEPPER_PAN_STEP_PORT   GPIOA
#define STEPPER_PAN_STEP_PIN    GPIO_Pin_0
#define STEPPER_TILT_STEP_PORT  GPIOA
#define STEPPER_TILT_STEP_PIN   GPIO_Pin_1

#define STEPPER_PAN_DIR_PORT    GPIOB
#define STEPPER_PAN_DIR_PIN     GPIO_Pin_0
#define STEPPER_TILT_DIR_PORT   GPIOB
#define STEPPER_TILT_DIR_PIN    GPIO_Pin_2

void Stepper_Init(void);
u8 Stepper_IsBusy(void);
u8 Stepper_StartMove(s32 pan_steps, s32 tilt_steps);
void Stepper_TIM2_IRQHandler(void);

#endif
