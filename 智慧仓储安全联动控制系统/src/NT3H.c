/*
 * Copyright (c) 2024 iSoftStone Education Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <string.h>

#include "iot_i2c.h"
#include "iot_errno.h"

#include "nfc.h"
#include "NT3H.h"

/* NT3H1101 / NT3H1201 共用 7-bit I2C 从机地址 */
#define NT3H_I2C_ADDR         (0x55)

/* 复用板载 I2C0(与 SHT30 / BH1750 同总线,地址不同不冲突) */
#define NT3H_I2C_HANDLE       EI2C0_M2
#define NT3H_I2C_FREQ         EI2C_FRE_400K

/* 头文件声明的全局变量在此处分配存储 */
uint8_t nfcPageBuffer[NFC_PAGE_SIZE];
NT3HerrNo errNo = NT3HERROR_NO_ERROR;

/* NT3H I2C 初始化标志,避免与 SHT30/BH1750 重复初始化同一总线 */
static int g_nt3h_i2c_inited = 0;

/***************************************************************
 * 函数名称: NT3HI2cInit
 * 说    明: 初始化 NT3H 使用的 I2C 总线(幂等)
 * 参    数: 无
 * 返 回 值: 0 成功, 其他失败
 ***************************************************************/
unsigned int NT3HI2cInit(void)
{
    if (g_nt3h_i2c_inited) {
        return IOT_SUCCESS;
    }
    unsigned int ret = IoTI2cInit(NT3H_I2C_HANDLE, NT3H_I2C_FREQ);
    if (ret == IOT_SUCCESS) {
        g_nt3h_i2c_inited = 1;
    } else {
        printf("[NT3H] I2C init failed: 0x%x\n", ret);
    }
    return ret;
}

/***************************************************************
 * 函数名称: NT3HI2cDeInit
 * 说    明: 释放 NT3H 标记(总线由 SHT30/BH1750 共享,不解销)
 * 参    数: 无
 * 返 回 值: 0
 ***************************************************************/
unsigned int NT3HI2cDeInit(void)
{
    g_nt3h_i2c_inited = 0;
    return IOT_SUCCESS;
}

/***************************************************************
 * 函数名称: NT3HGetNxpSerialNumber
 * 说    明: 读取 NXP 制造数据 / 序列号
 * 参    数: buffer - 输出缓冲,长度 >= 16
 * 返 回 值: 无
 ***************************************************************/
void NT3HGetNxpSerialNumber(char *buffer)
{
    if (buffer == NULL) {
        return;
    }
    uint8_t reg = MANUFACTORING_DATA_REG;
    if (IoTI2cWrite(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, &reg, 1) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_HEADER;
        return;
    }
    memset(buffer, 0, NFC_PAGE_SIZE);
    if (IoTI2cRead(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, (uint8_t *)buffer,
                   NFC_PAGE_SIZE) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_HEADER;
    }
}

/***************************************************************
 * 函数名称: NT3HReaddManufactoringData
 * 说    明: 读取 page 0 制造数据
 * 参    数: manuf - 16 字节输出
 * 返 回 值: true/false
 ***************************************************************/
bool NT3HReaddManufactoringData(uint8_t *manuf)
{
    if (manuf == NULL) {
        errNo = NT3HERROR_INVALID_USER_MEMORY_PAGE;
        return false;
    }
    uint8_t reg = MANUFACTORING_DATA_REG;
    if (IoTI2cWrite(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, &reg, 1) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_HEADER;
        return false;
    }
    memset(manuf, 0, NFC_PAGE_SIZE);
    if (IoTI2cRead(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, manuf,
                   NFC_PAGE_SIZE) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_HEADER;
        return false;
    }
    return true;
}

/***************************************************************
 * 函数名称: NT3HReadUserData
 * 说    明: 读取用户区一整页(16 字节)到 nfcPageBuffer
 * 参    数: page - 寄存器号
 * 返 回 值: true/false
 ***************************************************************/
bool NT3HReadUserData(uint8_t page)
{
    if (page > USER_END_REG) {
        errNo = NT3HERROR_INVALID_USER_MEMORY_PAGE;
        return false;
    }
    uint8_t reg = page;
    if (IoTI2cWrite(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, &reg, 1) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_USER_MEMORY_PAGE;
        return false;
    }
    memset(nfcPageBuffer, 0, NFC_PAGE_SIZE);
    if (IoTI2cRead(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, nfcPageBuffer,
                   NFC_PAGE_SIZE) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_USER_MEMORY_PAGE;
        return false;
    }
    return true;
}

/***************************************************************
 * 函数名称: NT3HWriteUserData
 * 说    明: 写一整页用户数据(16 字节)到指定 page
 * 参    数: page - 寄存器号, data - 16 字节输入
 * 返 回 值: true/false
 ***************************************************************/
bool NT3HWriteUserData(uint8_t page, const uint8_t *data)
{
    if (page > USER_END_REG || data == NULL) {
        errNo = NT3HERROR_INVALID_USER_MEMORY_PAGE;
        return false;
    }
    uint8_t buf[NFC_PAGE_SIZE + 1];
    buf[0] = page;
    memcpy(&buf[1], data, NFC_PAGE_SIZE);
    if (IoTI2cWrite(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, buf,
                    NFC_PAGE_SIZE + 1) != IOT_SUCCESS) {
        errNo = NT3HERROR_WRITE_USER_MEMORY_PAGE;
        return false;
    }
    return true;
}

/***************************************************************
 * 函数名称: NT3HEraseAllTag
 * 说    明: 把整个用户区清零(NDEF TLV 终止符 0xFE 留在 page 1[0])
 * 参    数: 无
 * 返 回 值: true/false
 ***************************************************************/
bool NT3HEraseAllTag(void)
{
    uint8_t zero[NFC_PAGE_SIZE] = {0};

    /* page 0 不允许写,跳过 */
    for (uint8_t page = USER_START_REG; page <= USER_END_REG; page++) {
        if (!NT3HWriteUserData(page, zero)) {
            return false;
        }
    }
    /* 在 page 1[0] 放 NDEF 终止符,符合 NDEF 规范 */
    uint8_t terminator[NFC_PAGE_SIZE] = {0};
    terminator[0] = 0xFE;
    return NT3HWriteUserData(USER_START_REG, terminator);
}

/***************************************************************
 * 函数名称: NT3HResetUserData
 * 说    明: 等价于 NT3HEraseAllTag(API 别名)
 * 参    数: 无
 * 返 回 值: true/false
 ***************************************************************/
bool NT3HResetUserData(void)
{
    return NT3HEraseAllTag();
}

/***************************************************************
 * 函数名称: NT3HReadHeaderNfc
 * 说    明: 读取 NDEF 头字节 + 终止指针
 * 参    数: endRecordsPtr - page1[1] 终止指针
 *           ndefHeader    - page1[0] 头 TLV tag
 * 返 回 值: true/false
 ***************************************************************/
bool NT3HReadHeaderNfc(uint8_t *endRecordsPtr, uint8_t *ndefHeader)
{
    if (!NT3HReadUserData(USER_START_REG)) {
        return false;
    }
    if (ndefHeader) {
        *ndefHeader = nfcPageBuffer[0];
    }
    if (endRecordsPtr) {
        *endRecordsPtr = nfcPageBuffer[1];
    }
    return true;
}

/***************************************************************
 * 函数名称: NT3HWriteHeaderNfc
 * 说    明: 写 NDEF 头字节 + 终止指针到 page 1
 * 参    数: endRecordsPtr - 终止指针
 *           ndefHeader    - 头 TLV tag
 * 返 回 值: true/false
 ***************************************************************/
bool NT3HWriteHeaderNfc(uint8_t endRecordsPtr, uint8_t ndefHeader)
{
    uint8_t pageData[NFC_PAGE_SIZE] = {0};
    pageData[0] = ndefHeader;
    pageData[1] = endRecordsPtr;
    return NT3HWriteUserData(USER_START_REG, pageData);
}

/***************************************************************
 * 函数名称: NT3HReadSession
 * 说    明: 读 SESSION 寄存器(0xFE)到 nfcPageBuffer
 * 参    数: 无
 * 返 回 值: true/false
 ***************************************************************/
bool NT3HReadSession(void)
{
    uint8_t reg = SESSION_REG;
    if (IoTI2cWrite(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, &reg, 1) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_HEADER;
        return false;
    }
    memset(nfcPageBuffer, 0, NFC_PAGE_SIZE);
    if (IoTI2cRead(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, nfcPageBuffer,
                   NFC_PAGE_SIZE) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_HEADER;
        return false;
    }
    return true;
}

/***************************************************************
 * 函数名称: getSessionReg
 * 说    明: NT3HReadSession 的别名
 ***************************************************************/
bool getSessionReg(void)
{
    return NT3HReadSession();
}

/***************************************************************
 * 函数名称: NT3HReadSram
 * 说    明: 读 SRAM 起始 16 字节到 nfcPageBuffer
 ***************************************************************/
bool NT3HReadSram(void)
{
    uint8_t reg = SRAM_START_REG;
    if (IoTI2cWrite(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, &reg, 1) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_HEADER;
        return false;
    }
    memset(nfcPageBuffer, 0, NFC_PAGE_SIZE);
    if (IoTI2cRead(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, nfcPageBuffer,
                   NFC_PAGE_SIZE) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_HEADER;
        return false;
    }
    return true;
}

/***************************************************************
 * 函数名称: NT3HReadConfiguration
 * 说    明: 读 CONFIG 寄存器(0x7A)到 *configuration
 ***************************************************************/
bool NT3HReadConfiguration(uint8_t *configuration)
{
    if (configuration == NULL) {
        return false;
    }
    uint8_t reg = CONFIG_REG;
    if (IoTI2cWrite(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, &reg, 1) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_HEADER;
        return false;
    }
    uint8_t val = 0;
    if (IoTI2cRead(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, &val, 1) != IOT_SUCCESS) {
        errNo = NT3HERROR_READ_HEADER;
        return false;
    }
    *configuration = val;
    return true;
}

/***************************************************************
 * 函数名称: getNxpUserData
 * 说    明: 读 page 1 起始 16 字节到 buffer(供上层 NDEF 解析)
 ***************************************************************/
bool getNxpUserData(char *buffer)
{
    if (buffer == NULL) {
        return false;
    }
    if (!NT3HReadUserData(USER_START_REG)) {
        return false;
    }
    memcpy(buffer, nfcPageBuffer, NFC_PAGE_SIZE);
    return true;
}
