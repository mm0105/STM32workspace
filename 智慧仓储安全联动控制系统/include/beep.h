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

#ifndef __BEEP_H__
#define __BEEP_H__

#include "smart_home_event.h"

typedef enum {
    BEEP_PRIO_LOW = 0,    /* 按键/确认/迎宾 */
    BEEP_PRIO_MID,        /* 布防/撤防/NFC OK */
    BEEP_PRIO_HIGH,       /* 入侵 */
    BEEP_PRIO_CRITICAL,   /* 火灾 - 立即打断其他 */
} beep_priority_t;

void beep_init(void);
/* 业务层调用: 触发一次指定类型的蜂鸣 */
void beep_request(beep_type_t type);
/* 强制静默(例如火灾解除) */
void beep_silence(void);
/* 查询当前是否在响(用于主循环判断) */
bool beep_is_active(void);

#endif
