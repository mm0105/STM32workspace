#include "stm32f10x.h"
#include "OLED_Font.h"

#define OLED_SCL_PORT GPIOB
#define OLED_SCL_PIN GPIO_Pin_6
#define OLED_SDA_PORT GPIOB
#define OLED_SDA_PIN GPIO_Pin_7

#define OLED_SCL(x) GPIO_WriteBit(OLED_SCL_PORT, OLED_SCL_PIN, (BitAction)(x))
#define OLED_SDA(x) GPIO_WriteBit(OLED_SDA_PORT, OLED_SDA_PIN, (BitAction)(x))
#define READ_SDA GPIO_ReadInputDataBit(OLED_SDA_PORT, OLED_SDA_PIN)

void OLED_I2C_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = OLED_SCL_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OLED_SCL_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = OLED_SDA_PIN;
    GPIO_Init(OLED_SDA_PORT, &GPIO_InitStructure);

    OLED_SCL(1);
    OLED_SDA(1);
}

void SDA_OUT(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Pin = OLED_SDA_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(OLED_SDA_PORT, &GPIO_InitStructure);
}

void SDA_IN(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Pin = OLED_SDA_PIN;
    GPIO_Init(OLED_SDA_PORT, &GPIO_InitStructure);
}

void OLED_I2C_Start(void)
{
    SDA_OUT();
    OLED_SDA(1);
    OLED_SCL(1);
    OLED_SDA(0);
    OLED_SCL(0);
}

void OLED_I2C_Stop(void)
{
    SDA_OUT();
    OLED_SCL(0);
    OLED_SDA(0);
    OLED_SCL(1);
    OLED_SDA(1);
}

uint8_t OLED_I2C_Wait_Ack(void)
{
    uint8_t tempTime = 0;
    OLED_SDA(1);
    SDA_IN();
    OLED_SCL(1);
    while (READ_SDA)
    {
        tempTime++;
        if (tempTime > 250)
        {
            OLED_I2C_Stop();
            return 1;
        }
    }
    OLED_SCL(0);
    return 0;
}

void OLED_I2C_Ack(void)
{
    OLED_SCL(0);
    SDA_OUT();
    OLED_SDA(0);
    OLED_SCL(1);
    OLED_SCL(0);
}

void OLED_I2C_NAck(void)
{
    OLED_SCL(0);
    SDA_OUT();
    OLED_SDA(1);
    OLED_SCL(1);
    OLED_SCL(0);
}

void OLED_I2C_SendByte(uint8_t Byte)
{
    uint8_t i;
    SDA_OUT();
    OLED_SCL(0);
    for (i = 0; i < 8; i++)
    {
        if (Byte & 0x80)
            OLED_SDA(1);
        else
            OLED_SDA(0);
        Byte <<= 1;
        OLED_SCL(1);
        OLED_SCL(0);
    }
}

uint8_t OLED_I2C_ReadByte(uint8_t ack)
{
    uint8_t i, receive = 0;
    SDA_IN();
    for (i = 0; i < 8; i++)
    {
        OLED_SCL(0);
        OLED_SCL(1);
        receive <<= 1;
        if (READ_SDA)
            receive++;
    }
    if (!ack)
        OLED_I2C_NAck();
    else
        OLED_I2C_Ack();
    return receive;
}

void OLED_WriteCommand(uint8_t Command)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_Wait_Ack();
    OLED_I2C_SendByte(0x00);
    OLED_I2C_Wait_Ack();
    OLED_I2C_SendByte(Command);
    OLED_I2C_Wait_Ack();
    OLED_I2C_Stop();
}

void OLED_WriteData(uint8_t Data)
{
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_Wait_Ack();
    OLED_I2C_SendByte(0x40);
    OLED_I2C_Wait_Ack();
    OLED_I2C_SendByte(Data);
    OLED_I2C_Wait_Ack();
    OLED_I2C_Stop();
}

void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand(0xB0 | Y);
    OLED_WriteCommand(0x10 | ((X & 0xF0) >> 4));
    OLED_WriteCommand(0x00 | (X & 0x0F));
}

/*
 * Write multiple data bytes (1 shared I2C Start/Stop, much faster)
 * len: data length, cursor must be set before calling
 */
void OLED_WriteDataMulti(const uint8_t *Data, uint8_t len)
{
    uint8_t i;
    OLED_I2C_Start();
    OLED_I2C_SendByte(0x78);
    OLED_I2C_Wait_Ack();
    OLED_I2C_SendByte(0x40);
    OLED_I2C_Wait_Ack();
    for (i = 0; i < len; i++)
    {
        OLED_I2C_SendByte(Data[i]);
        OLED_I2C_Wait_Ack();
    }
    OLED_I2C_Stop();
}

/*
 * Clear a specific line (covers the line's display area, 16 chars x 2 pages)
 * Used for "refresh single line" scenarios
 */
void OLED_ClearLine(uint8_t Line)
{
    uint8_t i;
    uint8_t y = (Line - 1) * 2;
    OLED_SetCursor(y, 0);
    for (i = 0; i < 64; i++) OLED_WriteData(0x00);
    OLED_SetCursor(y + 1, 0);
    for (i = 0; i < 64; i++) OLED_WriteData(0x00);
}

void OLED_Clear(void)
{
    uint8_t i, j;
    for (j = 0; j < 8; j++)
    {
        OLED_SetCursor(j, 0);
        for (i = 0; i < 128; i++)
        {
            OLED_WriteData(0x00);
        }
    }
}

void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t i;
    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i]);
    }
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
    for (i = 0; i < 8; i++)
    {
        OLED_WriteData(OLED_F8x16[Char - ' '][i + 8]);
    }
}

void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
    uint8_t i;
    for (i = 0; String[i] != '\0'; i++)
    {
        OLED_ShowChar(Line, Column + i, String[i]);
    }
}

/*
 * Fast string display - full line (16 chars) at once, 1 I2C Start/Stop
 * Used for "refresh single line" scenarios, ~8x faster than ShowString
 */
void OLED_ShowStringFast(uint8_t Line, uint8_t Column, char *String)
{
    uint8_t i, j, k = 0;
    uint8_t buf[128];

    /* Page 1 (8 bytes/char) */
    OLED_SetCursor((Line - 1) * 2, (Column - 1) * 8);
    for (i = 0; String[i] != '\0'; i++)
    {
        for (j = 0; j < 8; j++)
            buf[k++] = OLED_F8x16[String[i] - ' '][j];
    }
    OLED_WriteDataMulti(buf, k);
    k = 0;

    /* Page 2 (8 bytes/char) */
    OLED_SetCursor((Line - 1) * 2 + 1, (Column - 1) * 8);
    for (i = 0; String[i] != '\0'; i++)
    {
        for (j = 0; j < 8; j++)
            buf[k++] = OLED_F8x16[String[i] - ' '][j + 8];
    }
    OLED_WriteDataMulti(buf, k);
}

uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;
    while (Y--)
    {
        Result *= X;
    }
    return Result;
}

void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, Column + i, Number / OLED_Pow(10, Length - i - 1) % 10 + '0');
    }
}

void OLED_Init(void)
{
    uint32_t i, j;

    for (i = 0; i < 1000; i++)
    {
        for (j = 0; j < 1000; j++);
    }

    OLED_I2C_Init();

    OLED_WriteCommand(0xAE);

    OLED_WriteCommand(0xD5);
    OLED_WriteCommand(0x80);

    OLED_WriteCommand(0xA8);
    OLED_WriteCommand(0x3F);

    OLED_WriteCommand(0xD3);
    OLED_WriteCommand(0x00);

    OLED_WriteCommand(0x40);

    OLED_WriteCommand(0xA1);

    OLED_WriteCommand(0xC8);

    OLED_WriteCommand(0xDA);
    OLED_WriteCommand(0x12);

    OLED_WriteCommand(0x81);
    OLED_WriteCommand(0xCF);

    OLED_WriteCommand(0xD9);
    OLED_WriteCommand(0xF1);

    OLED_WriteCommand(0xDB);
    OLED_WriteCommand(0x30);

    OLED_WriteCommand(0xA4);

    OLED_WriteCommand(0xA6);

    OLED_WriteCommand(0x8D);
    OLED_WriteCommand(0x14);

    OLED_WriteCommand(0xAF);

    OLED_Clear();
}
