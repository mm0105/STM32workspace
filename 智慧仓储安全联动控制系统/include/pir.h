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

#ifndef __PIR_H__
#define __PIR_H__

#include "stdint.h"
#include "stdbool.h"

/* 人体感应联动模式 */
typedef enum {
    PIR_MODE_DISARM   = 0,  /* 撤防 - 不响应 */
    PIR_MODE_NIGHT    = 1,  /* 夜灯 - 检测到人开灯,延时关 */
    PIR_MODE_ARM      = 2,  /* 布防 - 检测到人蜂鸣器报警+开灯 */
} pir_mode_t;

typedef struct {
    pir_mode_t  mode;            /* 当前模式 */
    bool        presence;        /* 当前是否检测到人 */
    uint32_t    trigger_count;   /* 当日累计触发次数 */
    uint32_t    last_trigger_ms; /* 最近一次触发时间(相对 tick) */
} pir_status_t;

void     pir_init(void);
pir_status_t pir_get_status(void);
void     pir_set_mode(pir_mode_t mode);
pir_mode_t pir_get_mode(void);

#endif
