#include "stepper.h"

#define STEPPER_TIMER_PRESCALER      (72 - 1)
#define STEPPER_TIMER_PERIOD_US      (500 - 1)

typedef struct
{
    volatile u8 busy;
    volatile u8 pulse_high;
    volatile u32 tick_index;
    volatile u32 max_steps;
    volatile u32 pan_abs;
    volatile u32 tilt_abs;
} StepperState;

static StepperState g_stepper;

static u32 Stepper_Abs32(s32 value)
{
    return (value < 0) ? (u32)(-value) : (u32)value;
}

static void Stepper_SetDirPins(s32 pan_steps, s32 tilt_steps)
{
    if (pan_steps >= 0)
    {
        GPIO_SetBits(STEPPER_PAN_DIR_PORT, STEPPER_PAN_DIR_PIN);
    }
    else
    {
        GPIO_ResetBits(STEPPER_PAN_DIR_PORT, STEPPER_PAN_DIR_PIN);
    }

    if (tilt_steps >= 0)
    {
        GPIO_SetBits(STEPPER_TILT_DIR_PORT, STEPPER_TILT_DIR_PIN);
    }
    else
    {
        GPIO_ResetBits(STEPPER_TILT_DIR_PORT, STEPPER_TILT_DIR_PIN);
    }
}

static void Stepper_SetTimerEnabled(FunctionalState state)
{
    if (state == ENABLE)
    {
        TIM_SetCounter(TIM2, 0);
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        TIM_Cmd(TIM2, ENABLE);
    }
    else
    {
        TIM_Cmd(TIM2, DISABLE);
        GPIO_ResetBits(STEPPER_PAN_STEP_PORT, STEPPER_PAN_STEP_PIN);
        GPIO_ResetBits(STEPPER_TILT_STEP_PORT, STEPPER_TILT_STEP_PIN);
    }
}

void Stepper_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    GPIO_InitStructure.GPIO_Pin = STEPPER_PAN_STEP_PIN | STEPPER_TILT_STEP_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = STEPPER_PAN_DIR_PIN | STEPPER_TILT_DIR_PIN;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_ResetBits(GPIOA, STEPPER_PAN_STEP_PIN | STEPPER_TILT_STEP_PIN);
    GPIO_ResetBits(GPIOB, STEPPER_PAN_DIR_PIN | STEPPER_TILT_DIR_PIN);

    TIM_TimeBaseStructure.TIM_Period = STEPPER_TIMER_PERIOD_US;
    TIM_TimeBaseStructure.TIM_Prescaler = STEPPER_TIMER_PRESCALER;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM2, DISABLE);

    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    g_stepper.busy = 0;
    g_stepper.pulse_high = 0;
    g_stepper.tick_index = 0;
    g_stepper.max_steps = 0;
}

u8 Stepper_IsBusy(void)
{
    return g_stepper.busy;
}

u8 Stepper_StartMove(s32 pan_steps, s32 tilt_steps)
{
    u32 pan_abs = Stepper_Abs32(pan_steps);
    u32 tilt_abs = Stepper_Abs32(tilt_steps);
    u32 max_steps = (pan_abs > tilt_abs) ? pan_abs : tilt_abs;

    if (g_stepper.busy)
    {
        return 0;
    }
    if (max_steps == 0)
    {
        return 1;
    }

    __disable_irq();
    Stepper_SetDirPins(pan_steps, tilt_steps);
    g_stepper.pan_abs = pan_abs;
    g_stepper.tilt_abs = tilt_abs;
    g_stepper.max_steps = max_steps;
    g_stepper.tick_index = 0;
    g_stepper.pulse_high = 0;
    g_stepper.busy = 1;
    Stepper_SetTimerEnabled(ENABLE);
    __enable_irq();

    return 1;
}

void Stepper_TIM2_IRQHandler(void)
{
    u32 index;

    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == RESET)
    {
        return;
    }
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

    if (!g_stepper.busy)
    {
        Stepper_SetTimerEnabled(DISABLE);
        return;
    }

    index = g_stepper.tick_index;
    if (!g_stepper.pulse_high)
    {
        if (index < g_stepper.pan_abs)
        {
            GPIO_SetBits(STEPPER_PAN_STEP_PORT, STEPPER_PAN_STEP_PIN);
        }
        if (index < g_stepper.tilt_abs)
        {
            GPIO_SetBits(STEPPER_TILT_STEP_PORT, STEPPER_TILT_STEP_PIN);
        }
        g_stepper.pulse_high = 1;
        return;
    }

    if (index < g_stepper.pan_abs)
    {
        GPIO_ResetBits(STEPPER_PAN_STEP_PORT, STEPPER_PAN_STEP_PIN);
    }
    if (index < g_stepper.tilt_abs)
    {
        GPIO_ResetBits(STEPPER_TILT_STEP_PORT, STEPPER_TILT_STEP_PIN);
    }

    g_stepper.tick_index++;
    g_stepper.pulse_high = 0;

    if (g_stepper.tick_index >= g_stepper.max_steps)
    {
        g_stepper.busy = 0;
        Stepper_SetTimerEnabled(DISABLE);
    }
}
