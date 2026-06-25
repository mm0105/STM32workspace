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
#include "los_task.h"
#include "ohos_init.h"

#include "iot_gpio.h"
#include "iot_errno.h"
#include "iot_i2c.h"

#include "nfc.h"
#include "NT3H.h"
#include "nfc_app.h"
#include "smart_home_event.h"
#include "beep.h"

/* NT3H SESSION 寄存器 FIELD_PRESENT 位 */
#define NT3H_SESSION_REG        0xFE
#define NT3H_FIELD_PRESENT_BIT  0x10

/* NT3H.c 内部缓存(NT3HReadUserData/ReadSession 等共用) */
extern uint8_t nfcPageBuffer[NFC_PAGE_SIZE];

static volatile bool g_nfc_irq_flag = false;
static volatile nfc_tap_result_t g_nfc_last_result = NFC_TAP_INVALID;

static void nfc_fd_isr(char *args)
{
    (void)args;
    g_nfc_irq_flag = true;

    /* 立刻发送事件(主循环处理) */
    event_info_t evt = {0};
    evt.event = event_nfc_tap;
    /* 将 pir_state 字段复用为 1 表示有 tap,具体结果由 nfc_app_check_admin 解析 */
    evt.data.pir_state = 1;
    smart_home_event_send(&evt);
}

static void nfc_fd_init(void)
{
    /* 板上 FD 引脚若已飞线到 NFC_FD_GPIO,则启用下降沿中断(进场高电平) */
    IoTGpioInit(NFC_FD_GPIO);
    IoTGpioSetDir(NFC_FD_GPIO, IOT_GPIO_DIR_IN);
    if (IoTGpioRegisterIsrFunc(NFC_FD_GPIO,
        IOT_INT_TYPE_EDGE, IOT_GPIO_EDGE_FALL_LEVEL_LOW,
        nfc_fd_isr, NULL) != IOT_SUCCESS) {
        printf("[nfc_app] fd isr register failed (no wiring? ignore)\n");
        return;
    }
    IoTGpioSetIsrMask(NFC_FD_GPIO, FALSE);
}

/* NT3H I2C 地址与总线句柄 (与 NT3H.c 中保持一致) */
#define NT3H_I2C_ADDR           (0x55)
#define NT3H_I2C_HANDLE         EI2C0_M2

/* 读取 SESSION 寄存器判断当前是否在场.
 * 优先用 I2C 读 SESSION 寄存器, 失败则回退到 FD 引脚电平检测. */
static bool nfc_poll_field_present(void)
{
    /* 方式 1: I2C 读 SESSION 寄存器 (0xFE) 的 FIELD_PRESENT 位 */
    uint8_t reg = NT3H_SESSION_REG;
    if (IoTI2cWrite(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, &reg, 1) == IOT_SUCCESS) {
        uint8_t val = 0;
        if (IoTI2cRead(NT3H_I2C_HANDLE, NT3H_I2C_ADDR, &val, 1) == IOT_SUCCESS) {
            printf("[nfc] session=0x%02x\n", val);
            if (val & NT3H_FIELD_PRESENT_BIT) {
                return true;
            }
        } else {
            printf("[nfc] i2c read fail\n");
        }
    } else {
        printf("[nfc] i2c write fail\n");
    }

    /* 方式 2: 回退 - 直接读 FD 引脚电平 (低电平 = 有场) */
    IotGpioValue fd_level = IOT_GPIO_VALUE1;
    if (IoTGpioGetInputVal(NFC_FD_GPIO, &fd_level) == IOT_SUCCESS) {
        printf("[nfc] fd_pin=%d\n", fd_level);
        if (fd_level == IOT_GPIO_VALUE0) {
            return true;
        }
    }

    return false;
}

/* 读取 NDEF 用户区 page 1~3 (共 48 字节), 搜索 token 子串 */
static bool nfc_check_token_in_user_data(void)
{
    int  len = (int)strlen(NFC_ADMIN_TOKEN);
    if (len <= 0 || len > 48) {
        return false;
    }

    /* 拼接 3 页到 buf */
    uint8_t buf[48] = {0};
    for (int p = 0; p < 3; p++) {
        if (NT3HReadUserData((uint8_t)(USER_START_REG + p)) != true) {
            return false;
        }
        memcpy(&buf[p * NFC_PAGE_SIZE], nfcPageBuffer, NFC_PAGE_SIZE);
    }

    /* 在 48 字节内搜索 token */
    for (int off = 0; off <= (int)(sizeof(buf) - len); off++) {
        if (memcmp(&buf[off], NFC_ADMIN_TOKEN, len) == 0) {
            return true;
        }
    }
    return false;
}

nfc_tap_result_t nfc_app_check_admin(void)
{
    /* 优先检查 FD 中断标志(ISR 已触发过) */
    if (g_nfc_irq_flag) {
        g_nfc_irq_flag = false;
        g_nfc_last_result = NFC_TAP_OK;
        beep_request(BEEP_NFC_OK);
        return g_nfc_last_result;
    }

    /* 检测到 NFC 场即视为管理员刷卡, 不校验 token */
    if (nfc_poll_field_present()) {
        g_nfc_last_result = NFC_TAP_OK;
        beep_request(BEEP_NFC_OK);
        return g_nfc_last_result;
    }
    g_nfc_last_result = NFC_TAP_NONE;
    return g_nfc_last_result;
}

nfc_tap_result_t nfc_app_last_result(void)
{
    return g_nfc_last_result;
}

void nfc_app_init(void)
{
    smart_home_event_init();

    unsigned int ret = nfc_init();
    if (ret != 0) {
        printf("[nfc_app] nfc_init failed: %d\n", ret);
        /* 继续 - 允许系统在没有 NFC 模块的板上也能跑 */
    } else {
        /* 预写管理员 token 到 NDEF,只写一次 */
        nfc_store_text(NDEFFirstPos, (uint8_t *)NFC_ADMIN_TOKEN);
    }

    nfc_fd_init();

    printf("[nfc_app] init ok\n");
}

APP_FEATURE_INIT(nfc_app_init);
