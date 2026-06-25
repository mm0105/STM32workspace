#include "usart.h"
#include "misc.h"
#include "stepper.h"
#include <stdlib.h>
#include <string.h>

static volatile char g_rx_line[USART1_LINE_MAX];
static volatile u8 g_rx_index = 0;
static volatile u8 g_line_ready = 0;
static volatile u8 g_waiting_move_done = 0;
static volatile u8 g_rx_overflow = 0;

static void USART1_SendChar(char ch)
{
    USART_SendData(USART1, (u16)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
    {
    }
}

static u8 ParseMoveLine(char *line, s32 *pan_steps, s32 *tilt_steps)
{
    char *cmd;
    char *pan;
    char *tilt;
    char *end;

    cmd = strtok(line, " \t\r\n");
    pan = strtok(NULL, " \t\r\n");
    tilt = strtok(NULL, " \t\r\n");

    if (cmd == 0 || pan == 0 || tilt == 0 || strcmp(cmd, "MOVE") != 0)
    {
        return 0;
    }

    end = 0;
    *pan_steps = (s32)strtol(pan, &end, 10);
    if (end == pan || *end != '\0')
    {
        return 0;
    }

    end = 0;
    *tilt_steps = (s32)strtol(tilt, &end, 10);
    if (end == tilt || *end != '\0')
    {
        return 0;
    }

    return 1;
}

static char *TrimLine(char *line)
{
    char *end;

    while (*line == ' ' || *line == '\t')
    {
        line++;
    }

    end = line + strlen(line);
    while (end > line && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    {
        *--end = '\0';
    }

    return line;
}

void USART1_Init(u32 bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    USART_Cmd(USART1, ENABLE);
    USART_ClearFlag(USART1, USART_FLAG_TC);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void USART1_SendString(const char *str)
{
    while (*str)
    {
        USART1_SendChar(*str++);
    }
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
    {
    }
}

void USART1_IRQHandler_User(void)
{
    u8 ch;

    if (USART_GetITStatus(USART1, USART_IT_RXNE) == RESET)
    {
        return;
    }

    ch = (u8)USART_ReceiveData(USART1);
    if (ch == '\r' || ch == '\n')
    {
        if (g_rx_overflow)
        {
            g_rx_overflow = 0;
            g_rx_index = 0;
            g_line_ready = 0;
            return;
        }

        if (g_rx_index == 0)
        {
            return;
        }

        g_rx_line[g_rx_index] = '\0';
        g_rx_index = 0;
        g_line_ready = 1;
        return;
    }

    if (g_rx_index < (USART1_LINE_MAX - 1))
    {
        g_rx_line[g_rx_index++] = (char)ch;
    }
    else
    {
        g_rx_index = 0;
        g_line_ready = 0;
        g_rx_overflow = 1;
    }
}

void USART1_ProcessCommand(void)
{
    char line[USART1_LINE_MAX];
    char *cmd;
    s32 pan_steps;
    s32 tilt_steps;

    if (g_waiting_move_done && !Stepper_IsBusy())
    {
        g_waiting_move_done = 0;
        USART1_SendString("OK\r\n");
    }

    if (!g_line_ready)
    {
        return;
    }

    __disable_irq();
    strncpy(line, (const char *)g_rx_line, USART1_LINE_MAX);
    line[USART1_LINE_MAX - 1] = '\0';
    g_line_ready = 0;
    __enable_irq();

    cmd = TrimLine(line);
    if (*cmd == '\0')
    {
        return;
    }
    if (strcmp(cmd, "PING") == 0)
    {
        USART1_SendString("PONG\r\n");
        return;
    }
    if (!ParseMoveLine(cmd, &pan_steps, &tilt_steps))
    {
        USART1_SendString("ERR\r\n");
        return;
    }

    if (Stepper_IsBusy())
    {
        USART1_SendString("BUSY\r\n");
        return;
    }

    if (!Stepper_StartMove(pan_steps, tilt_steps))
    {
        USART1_SendString("BUSY\r\n");
        return;
    }

    if (Stepper_IsBusy())
    {
        g_waiting_move_done = 1;
        return;
    }

    USART1_SendString("OK\r\n");
}
