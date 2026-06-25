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

#ifndef __UI_PAGES_H__
#define __UI_PAGES_H__

#include "stdint.h"
#include "stdbool.h"
#include "pir.h"
#include "smart_home_event.h"

/* 4 个界面(顺序: 左/右键循环切换, FIRE 不参与循环) */
typedef enum {
    UI_PAGE_HOME      = 0,  /* 主控 - 灯光/风扇/温湿度/光照/火灾 面板 */
    UI_PAGE_SECURITY  = 1,  /* 安防 - 人体感应 3 模式切换 + 状态 */
    UI_PAGE_AUTO      = 2,  /* 自动 - 当前自动模式开关 + 阈值快速概览 */
    UI_PAGE_FIRE      = 3,  /* 火灾 - 全屏大字 + NFC 提示 (特殊页, 不参与循环) */
    UI_PAGE_MAX,
} ui_page_id_t;

/* 可参与左右键循环的页面(去掉 FIRE) */
#define UI_PAGE_NAV_COUNT  3

/* 阈值集合 */
typedef struct {
    uint16_t light_on;        /* 单位 lx */
    uint16_t light_off;
    uint16_t fan_temp;        /* 单位 ℃ */
    uint16_t fan_humi;        /* 单位 % */
    uint16_t fire_ppm;        /* 单位 ppm */
    uint8_t  pir_mode;        /* pir_mode_t */
} threshold_t;

/* 系统运行状态机(主控访问) */
typedef struct {
    ui_page_id_t   current_page;
    bool           auto_mode;          /* 全局自动模式开关 */
    bool           fire_alarm_active;  /* 当前是否在火灾状态 */
    threshold_t    thr;
    pir_mode_t     pir_mode;
    /* AUTO 页: 6 个阈值 + auto_mode 开关, 默认选中第 0 项
     *   idx=0: auto_mode (toggle)
     *   idx=1: light_on
     *   idx=2: light_off
     *   idx=3: fan_temp
     *   idx=4: fan_humi
     *   idx=5: fire_ppm
     */
    int            auto_selected;      /* AUTO 页当前选中行 0..5 */
    bool           auto_editing;       /* AUTO 页编辑态 */
    bool           dirty;              /* 状态变化标记, 由主循环检测后 partial refresh */
} ui_state_t;

ui_state_t *ui_get_state(void);

/* 初始化 UI 状态机默认值 */
void ui_state_init(void);

/* 渲染当前页(整屏, 由主循环在切页时调用) */
void ui_render_current(void);

/* 局部刷新(用于同页状态变化后的 partial redraw, 不擦整屏) */
void ui_refresh_partial(void);

/* 局部刷新主控页数据区: 温湿度/光照/火灾面板, 不重绘标题/菜单/状态栏.
 * 由主循环周期性调用(只在 HOME 页生效), 用于避免整屏反复刷新. */
void ui_refresh_dashboard_data(void);

/* 处理按键(主循环分发) */
void ui_on_key(uint8_t key_no);

/* 处理其他事件(PIR/NFC/火灾/防火解除) */
void ui_on_event(event_info_t *evt);

/* 触发进入火灾页(由 MQ2/火灾检测调用) */
void ui_enter_fire_page(void);
/* 强制退出火灾页(由 NFC admin 确认调用) */
void ui_exit_fire_page(void);

#endif
