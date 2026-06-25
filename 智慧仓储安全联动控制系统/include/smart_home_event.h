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

#ifndef __SMART_HOME_EVENT_H__
#define __SMART_HOME_EVENT_H__

#include "stdint.h"
#include "stdbool.h"



typedef enum event_type{

    event_key_press = 1,
    event_iot_cmd,
    event_su03t,
    event_beep_request,   /* 蜂鸣器请求 */
    event_pir_trigger,    /* 人体感应触发 */
    event_nfc_tap,        /* NFC 刷卡 */
    event_fire_alarm,     /* 火灾报警 */
    event_fire_clear,     /* 火灾解除 */
    event_timer_tick,     /* 1s 软定时器 */
    event_uart_recv,      /* 串口数据(语音/传感器) */

}event_type_t;

/* 蜂鸣器音调类型(业务级抽象,与具体频率解耦) */
typedef enum {
    BEEP_NONE = 0,
    BEEP_KEY_CLICK,        /* 按键短哔 */
    BEEP_CONFIRM,          /* 确认音(双短) */
    BEEP_FIRE_ALARM,       /* 火灾急促 */
    BEEP_INTRUDER,         /* 入侵长鸣 */
    BEEP_GREETING,         /* 迎宾叮咚 */
    BEEP_ARM,              /* 布防 */
    BEEP_DISARM,           /* 撤防 */
    BEEP_NFC_OK,           /* NFC 验证通过 */
    BEEP_FIRE_CLEAR,       /* 火灾解除(下行音) */
} beep_type_t;

typedef struct event_info
{
    event_type_t event;

    union {
        uint8_t key_no;
        int iot_data;
        int su03t_data;
        beep_type_t beep_type;
        uint8_t  pir_state;       /* 0: 离开, 1: 检测到人 */
        uint8_t  fire_active;     /* 0: 解除, 1: 报警 */
        uint8_t  nfc_id[8];       /* NFC UID 或 NDEF 文本 */
        uint32_t tick_ms;
        uint8_t  uart_byte;
    } data;
} event_info_t;

void smart_home_event_init(void);
int smart_home_event_send(event_info_t *event);
int smart_home_event_wait(event_info_t *event, int timeoutMs);

#endif
