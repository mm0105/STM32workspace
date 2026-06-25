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

#include "pir.h"
#include "beep.h"
#include "smart_home_event.h"
#include "smart_home.h"
#include "drv_light.h"
#include "drv_motor.h"

#define PIR_GPIO              GPIO0_PA3
#define PIR_THREAD_STACK      2048
#define PIR_THREAD_PRIO       22

/* 共享状态(本模块内) */
static volatile pir_mode_t  g_pir_mode      = PIR_MODE_DISARM;
static volatile bool        g_pir_presence  = false;
static volatile uint32_t    g_pir_count     = 0;
static volatile uint32_t    g_pir_last_tick = 0;

/* 夜灯延时关灯软定时器 */
static uint16_t  g_pir_night_timer = 0;
static const uint16_t NIGHT_LIGHT_TIMEOUT_S = 5;  /* 夜灯 5 秒后自动关灯 */

/* ISR 仅做标记,具体业务在 pir_thread 中处理 */
static volatile bool g_pir_irq_flag = false;

/* PIR 冷却: 防止人体检测过于频繁导致系统死机 */
static int g_pir_cooldown = 0;
#define PIR_COOLDOWN_S 3  /* 3 秒内不再响应新的 PIR 触发 */

static void pir_isr_func(void *args)
{
    (void)args;
    g_pir_irq_flag = true;
}

static void pir_handle_event(void)
{
    if (!g_pir_irq_flag) {
        return;
    }
    g_pir_irq_flag = false;

    /* 冷却检查: 防止 PIR 频繁触发导致系统死机 */
    if (g_pir_cooldown > 0) {
        return;
    }

    /* 仅在 NIGHT / ARM 模式下响应人体感应,
     * DISARM 完全不工作. */
    if (g_pir_mode == PIR_MODE_DISARM) {
        return;
    }

    g_pir_cooldown = PIR_COOLDOWN_S;

    g_pir_presence = true;
    g_pir_count++;
    g_pir_last_tick = LOS_TickCountGet();

    /* 发送事件给主循环,用于刷新 UI */
    event_info_t evt = {0};
    evt.event = event_pir_trigger;
    evt.data.pir_state = 1;
    smart_home_event_send(&evt);

    /* 按当前模式联动 */
    switch (g_pir_mode) {
        case PIR_MODE_NIGHT:
            /* 夜灯: 只开灯, 不蜂鸣 */
            g_pir_night_timer = NIGHT_LIGHT_TIMEOUT_S;
            light_set_state(true);
            lcd_set_light_state(true);
            break;

        case PIR_MODE_ARM:
            /* 布防: 蜂鸣器入侵报警 + 开灯 */
            beep_request(BEEP_INTRUDER);
            light_set_state(true);
            lcd_set_light_state(true);
            break;

        default:
            break;
    }
}

static void pir_thread(void *arg)
{
    (void)arg;
    while (1) {
        pir_handle_event();
        /* 1s 心跳 - 夜灯延时关灯(由线程自己计数,无需软定时器) */
        LOS_Msleep(1000);

        /* PIR 冷却倒计时 */
        if (g_pir_cooldown > 0) {
            g_pir_cooldown--;
        }

        if (g_pir_mode == PIR_MODE_NIGHT && g_pir_night_timer > 0) {
            g_pir_night_timer--;
            if (g_pir_night_timer == 0) {
                /* 关闭夜灯 - 物理关灯 + 发事件通知主循环刷 UI */
                light_set_state(false);
                lcd_set_light_state(false);
                event_info_t evt = {0};
                evt.event = event_pir_trigger;
                evt.data.pir_state = 0;
                g_pir_presence = false;
                smart_home_event_send(&evt);
            }
        }
    }
}

void pir_set_mode(pir_mode_t mode)
{
    /* 退出夜灯模式 -> 立即关闭 RGB 灯 */
    if (g_pir_mode == PIR_MODE_NIGHT && mode != PIR_MODE_NIGHT) {
        g_pir_night_timer = 0;
        light_set_state(false);
        lcd_set_light_state(false);
    }
    g_pir_mode = mode;
    printf("[pir] mode -> %d\n", (int)mode);
}

pir_mode_t pir_get_mode(void)
{
    return g_pir_mode;
}

pir_status_t pir_get_status(void)
{
    pir_status_t s = {
        .mode            = g_pir_mode,
        .presence        = g_pir_presence,
        .trigger_count   = g_pir_count,
        .last_trigger_ms = g_pir_last_tick,
    };
    return s;
}

void pir_init(void)
{
    unsigned int ret;

    smart_home_event_init();

    /* 初始化 GPIO(输入) */
    IoTGpioInit(PIR_GPIO);
    IoTGpioSetDir(PIR_GPIO, IOT_GPIO_DIR_IN);

    /* 注册上升沿中断 */
    ret = IoTGpioRegisterIsrFunc(PIR_GPIO,
        IOT_INT_TYPE_EDGE, IOT_GPIO_EDGE_RISE_LEVEL_HIGH,
        pir_isr_func, NULL);
    if (ret != IOT_SUCCESS) {
        printf("[pir] register isr failed(%d)\n", ret);
        return;
    }
    IoTGpioSetIsrMask(PIR_GPIO, FALSE);

    /* 创建 pir 处理线程(1s 心跳由线程内部实现,不需要软定时器) */
    TSK_INIT_PARAM_S task = {0};
    task.pfnTaskEntry = (TSK_ENTRY_FUNC)pir_thread;
    task.uwStackSize  = PIR_THREAD_STACK;
    task.pcName       = "pir";
    task.usTaskPrio   = PIR_THREAD_PRIO;
    unsigned int tid;
    ret = LOS_TaskCreate(&tid, &task);
    if (ret != LOS_OK) {
        printf("[pir] task create failed 0x%x\n", ret);
        return;
    }

    printf("[pir] init ok (mode=DISARM)\n");
}

APP_FEATURE_INIT(pir_init);
