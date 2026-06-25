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
#include "los_queue.h"
#include "ohos_init.h"

#include "iot_pwm.h"
#include "iot_gpio.h"
#include "iot_errno.h"

#include "beep.h"
#include "smart_home_event.h"

#define BEEP_PORT            EPWMDEV_PWM5_M0
#define BEEP_QUEUE_SIZE      8
#define BEEP_STACK_SIZE      2048
#define BEEP_TASK_PRIO       18

/* Hi3861V100 PWM: IoTPwmStart(port, duty, freq) 的 freq 参数为实际输出频率(Hz).
 * b7_beep 已验证: 1000~8000 Hz 范围人耳可闻.
 * 旧版误传 40000~64000 当做"分频系数", 实际输出 40~64kHz 超声, 完全听不到. */
#define BEEP_FREQ_BASE    3000   /* 3.0 kHz 基准音 */
#define BEEP_FREQ_LOW     2500   /* 2.5 kHz 低音 */
#define BEEP_FREQ_HIGH    4000   /* 4.0 kHz 高音 */

static unsigned int g_beep_queue = 0;
static bool         g_beep_active = false;

/* 一次蜂鸣的描述: 分频系数/占空比/持续时间(可重复)
 * 注: freq_hz 实际是 PWM 分频系数, 真实输出频率 = 160MHz / freq_hz.
 *     0 表示静音段(不发声). */
typedef struct {
    unsigned int freq_hz;
    unsigned int duty;     /* 0-100 */
    unsigned int ms;
} beep_note_t;

/* 一个完整的音型,若干 note + 间隔 */
typedef struct {
    const beep_note_t *notes;
    unsigned int note_cnt;
    beep_priority_t  prio;
} beep_pattern_t;

/* ================== 音型定义 (按需求文档频率) ================== */

/* 按键短哔: 3000Hz, 30ms */
static const beep_note_t pattern_key_click[] = {
    {3000, 50, 30},
};
/* 确认音: 短-短-短, 2700Hz */
static const beep_note_t pattern_confirm[] = {
    {2700, 50, 60}, {0, 0, 40}, {2700, 50, 60},
};
/* 火灾急促: 2000Hz↔2700Hz 交替, 200ms/次, 持续约 2.4s */
static const beep_note_t pattern_fire[] = {
    {2000, 60, 200}, {2700, 60, 200},
    {2000, 60, 200}, {2700, 60, 200},
    {2000, 60, 200}, {2700, 60, 200},
};
/* 入侵长鸣: 880Hz 持续约 1.6s */
static const beep_note_t pattern_intruder[] = {
    {880, 70, 200}, {880, 70, 200},
    {880, 70, 200}, {880, 70, 200},
    {880, 70, 200}, {880, 70, 200},
    {880, 70, 200}, {880, 70, 200},
};
/* 迎宾叮咚: 523→659→784Hz 上行, 100ms/级 */
static const beep_note_t pattern_greeting[] = {
    {523, 50, 100}, {659, 50, 100}, {784, 50, 100},
};
/* 布防音: 659Hz 200ms 单声 */
static const beep_note_t pattern_arm[] = {
    {659, 60, 200},
};
/* 撤防音: 523Hz 200ms 单声 */
static const beep_note_t pattern_disarm[] = {
    {523, 50, 200},
};
/* NFC OK: 880Hz 100ms */
static const beep_note_t pattern_nfc_ok[] = {
    {880, 55, 100},
};
/* 火灾解除: 659→523→349Hz 下行, 150ms/级 */
static const beep_note_t pattern_fire_clear[] = {
    {659, 55, 150}, {523, 55, 150}, {349, 55, 150},
};

static const beep_pattern_t g_patterns[] = {
    [BEEP_KEY_CLICK]  = {pattern_key_click,  1, BEEP_PRIO_MID},
    [BEEP_CONFIRM]    = {pattern_confirm,    3, BEEP_PRIO_LOW},
    [BEEP_FIRE_ALARM] = {pattern_fire,       6, BEEP_PRIO_CRITICAL},
    [BEEP_INTRUDER]   = {pattern_intruder,   8, BEEP_PRIO_HIGH},
    [BEEP_GREETING]   = {pattern_greeting,   3, BEEP_PRIO_LOW},
    [BEEP_ARM]        = {pattern_arm,        1, BEEP_PRIO_MID},
    [BEEP_DISARM]     = {pattern_disarm,     1, BEEP_PRIO_MID},
    [BEEP_NFC_OK]     = {pattern_nfc_ok,     1, BEEP_PRIO_MID},
    [BEEP_FIRE_CLEAR] = {pattern_fire_clear, 3, BEEP_PRIO_MID},
};

/* 启动 PWM 输出
 * 注: b7_beep 已验证, IoTPwmStart 的 freq 参数为实际 Hz 值, 非分频系数. */
static void beep_hw_on(unsigned int freq, unsigned int duty)
{
    if (duty > 100) duty = 100;
    IoTPwmStart(BEEP_PORT, duty, freq);
}

static void beep_hw_off(void)
{
    IoTPwmStop(BEEP_PORT);
}

static void beep_play_pattern(beep_type_t type)
{
    if (type <= BEEP_NONE || type >= BEEP_FIRE_CLEAR + 1) {
        return;
    }
    const beep_pattern_t *p = &g_patterns[type];
    printf("[beep] play type=%d, notes=%u, prio=%d\n", type, p->note_cnt, p->prio);
    g_beep_active = true;
    for (unsigned int i = 0; i < p->note_cnt; i++) {
        const beep_note_t *n = &p->notes[i];
        if (n->freq_hz == 0) {
            beep_hw_off();
        } else {
            beep_hw_on(n->freq_hz, n->duty);
        }
        LOS_Msleep(n->ms);
    }
    beep_hw_off();
    g_beep_active = false;
}

/* 蜂鸣器线程: 等待事件队列,根据 type 播放对应音型 */
static void beep_thread(void *arg)
{
    (void)arg;
    event_info_t evt;
    unsigned int ret;

    while (1) {
        ret = LOS_QueueReadCopy(g_beep_queue, &evt, sizeof(evt), LOS_WAIT_FOREVER);
        if (ret != LOS_OK) {
            continue;
        }
        if (evt.event != event_beep_request) {
            continue;
        }
        beep_play_pattern(evt.data.beep_type);
    }
}

void beep_request(beep_type_t type)
{
    event_info_t evt = {0};
    evt.event = event_beep_request;
    evt.data.beep_type = type;
    if (g_beep_queue != 0) {
        unsigned int ret = LOS_QueueWriteCopy(g_beep_queue, &evt, sizeof(evt), 0);
        if (ret != LOS_OK) {
            printf("[beep] request type=%d queue write FAILED: %u\n", type, ret);
        }
    } else {
        printf("[beep] request type=%d queue not ready\n", type);
    }
}

void beep_silence(void)
{
    beep_hw_off();
    g_beep_active = false;
}

bool beep_is_active(void)
{
    return g_beep_active;
}

void beep_init(void)
{
    unsigned int ret;

    smart_home_event_init();

    /* 初始化 PWM 通道. 注意: 这里即使 PWM 初始化失败也不能直接 return,
     * 否则队列和线程都不会被创建, 后续所有 beep_request 都会被静默丢弃.
     * 改为仅打印错误继续执行, 蜂鸣器线程内部会做硬件自检, 这样诊断更清晰. */
    ret = IoTPwmInit(BEEP_PORT);
    printf("[beep] IoTPwmInit(BEEP_PORT=0x%x) ret=%d\n",
           (unsigned)BEEP_PORT, ret);
    if (ret != 0) {
        printf("[beep] IoTPwmInit failed(%d), beep hw disabled\n", ret);
    }

    /* 创建蜂鸣器事件队列 */
    ret = LOS_QueueCreate("beep_q", BEEP_QUEUE_SIZE, &g_beep_queue, 0, sizeof(event_info_t));
    if (ret != LOS_OK) {
        printf("[beep] queue create failed 0x%x\n", ret);
        return;
    }

    /* 创建蜂鸣器线程 */
    TSK_INIT_PARAM_S task = {0};
    task.pfnTaskEntry = (TSK_ENTRY_FUNC)beep_thread;
    task.uwStackSize  = BEEP_STACK_SIZE;
    task.pcName       = "beep";
    task.usTaskPrio   = BEEP_TASK_PRIO;
    unsigned int tid;
    ret = LOS_TaskCreate(&tid, &task);
    if (ret != LOS_OK) {
        printf("[beep] task create failed 0x%x\n", ret);
        return;
    }
    printf("[beep] init ok\n");

    /* 启动后立刻长鸣 500ms 一次, 验证硬件通路.
     * 50ms 短音人耳难以捕捉, 这里直接走 PWM 通道, 不依赖队列/线程,
     * 能更可靠地判断蜂鸣器/PWM 是否正常. */
    LOS_Msleep(300);
    printf("[beep] self-test: IoTPwmStart(%d, 50) for 500ms\n", BEEP_FREQ_BASE);
    IoTPwmStart(BEEP_PORT, 50, BEEP_FREQ_BASE);
    LOS_Msleep(500);
    IoTPwmStop(BEEP_PORT);
    printf("[beep] self-test done\n");

    /* 紧接着再发一个普通事件, 验证队列/线程通路 */
    LOS_Msleep(200);
    beep_request(BEEP_KEY_CLICK);
}

APP_FEATURE_INIT(beep_init);
