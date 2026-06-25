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

#ifndef __NFC_APP_H__
#define __NFC_APP_H__

#include "stdint.h"
#include "stdbool.h"

/* 系统预设的管理员卡 NDEF 文本(用于比对) */
#define NFC_ADMIN_TOKEN         "your-admin-token"
/* FD 中断对应的 GPIO(若硬件未连 FD 引脚,可注释 NFC_FD_GPIO 退化为按键模拟) */
#define NFC_FD_GPIO             GPIO0_PB0

typedef enum {
    NFC_TAP_INVALID = 0,
    NFC_TAP_OK,         /* 读到 NDEF,匹配管理员 token */
    NFC_TAP_FAIL,       /* 读到 NDEF,但不是管理员 token */
    NFC_TAP_NONE,       /* 未检测到刷卡 */
} nfc_tap_result_t;

void nfc_app_init(void);
/* 由 FD 中断或按键触发: 一次刷卡确认 */
nfc_tap_result_t nfc_app_check_admin(void);
/* 查询最近一次结果 */
nfc_tap_result_t nfc_app_last_result(void);

#endif
