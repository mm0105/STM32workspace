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
#include <stdbool.h>
#include "los_task.h"
#include "ohos_init.h"

#include "lcd.h"
#include "picture.h"
#include "components.h"

#include "ui_pages.h"
#include "smart_home.h"
#include "beep.h"
#include "pir.h"
#include "nfc_app.h"
#include "adc_key.h"
#include "drv_light.h"
#include "drv_motor.h"

/* 全局 WiFi 状态: smart_home.c 中定义, ui_pages.c 渲染主页/切页时读.
 * 这里本地 extern 一份避免依赖 smart_home.h 同步更新. */
extern bool network_state;

static ui_state_t g_state = {
    .current_page    = UI_PAGE_HOME,
    .auto_mode       = false,
    .fire_alarm_active = false,
    .auto_selected   = 0,
    .auto_editing    = false,
    .thr = {
        .light_on  = 30,
        .light_off = 150,
        .fan_temp  = 32,
        .fan_humi  = 70,
        .fire_ppm  = 200,
        .pir_mode  = PIR_MODE_DISARM,
    },
    .pir_mode       = PIR_MODE_DISARM,
};

/* SECURITY 局部刷新用的"上次值"缓存: 避免每帧重画整屏
 * 初始化为 -1, 第一次调用时强制重画一次数据区. */
static int  sec_last_mode     = -1;
static int  sec_last_presence = -1;
static int  sec_last_count    = -1;

/* AUTO 局部刷新缓存: 避免 PIR 触发时整屏重刷 */
static int   auto_last_auto_mode    = -1;
static int   auto_last_selected     = -1;
static bool  auto_last_editing      = false;
static int   auto_last_thr[5]       = {-1, -1, -1, -1, -1};

ui_state_t *ui_get_state(void) { return &g_state; }

void ui_state_init(void)
{
    /* 把阈值同步到 pir 模块 */
    pir_set_mode((pir_mode_t)g_state.thr.pir_mode);
}

/* ============== 文本工具 ============== */
static const char *pir_mode_str(pir_mode_t m)
{
    switch (m) {
        case PIR_MODE_DISARM:  return "DISARM";
        case PIR_MODE_NIGHT:   return "NIGHT";
        case PIR_MODE_ARM:     return "ARM";
        default: return "UNKNOWN";
    }
}

/* HOME 页菜单区局部刷新:
 * 只擦写文字区域(约 100x24 像素/菜单), 图片仅在 icon 变化时重绘.
 * 避免每次 UP/DOWN 都刷整个 32..200 区域, 消除 SPI 长时间阻塞. */
static void ui_refresh_home_menus(void)
{
    if (g_state.current_page != UI_PAGE_HOME) return;
    int count = smart_home_get_menu_count();
    lcd_menu_update(lcd_menus, count, smart_home_get_menu_index());

    /* 缓存上次的图片指针, 仅在 icon 变化时重绘图片区 */
    static const unsigned char *last_menu_img[2] = {NULL, NULL};

    for (int i = 0; i < count; i++) {
        lcd_menu_t *menu = lcd_menus[i];
        if (menu == NULL) continue;

        /* 图片变化时重绘图片区域 */
        if (menu->img.img != last_menu_img[i]) {
            lcd_fill(menu->base_x, menu->base_y,
                     menu->base_x + menu->img.width,
                     menu->base_y + menu->img.height,
                     LCD_WHITE);
            lcd_show_picture(menu->base_x, menu->base_y,
                             menu->img.width, menu->img.height,
                             menu->img.img);
            last_menu_img[i] = menu->img.img;
        }

        /* 文字区域始终刷新 (选中状态变化、文字内容变化均需更新)
         * 3 个中文字符 24px 宽 = 72px, 预留 80px 防止白底覆盖右侧 DB 图片 */
        int text_y = menu->base_y + menu->img.height + 3;
        lcd_fill(menu->base_x, text_y,
                 menu->base_x + 80, text_y + 24,
                 LCD_WHITE);
        lcd_show_text(menu->base_x, text_y, menu->text.name,
                      menu->text.fc,
                      menu->is_selected ? LCD_BROWN : menu->text.bc,
                      menu->text.font_size, 0);
    }
}

/* 右上角 WiFi 图标统一绘制: 所有切页渲染时调用, 防止整屏擦时 wifi 消失. */
static void ui_draw_wifi_icon(void)
{
    lcd_show_picture(280, 0, 32, 32,
                     network_state ? img_wifi_on : img_wifi_off);
}

/* ============== 页面渲染 ============== */
static void ui_render_home(void)
{
    lcd_fill(0, 0, LCD_W, LCD_H, LCD_WHITE);
    lcd_show_text(110, 0, "智慧仓库", LCD_RED, LCD_WHITE, 16, 0);
    /* WiFi 图标在 ui_render_current 末尾统一画, 此处不重复 */
    lcd_menu_update(lcd_menus, smart_home_get_menu_count(), smart_home_get_menu_index());
    lcd_menu_show(lcd_menus, smart_home_get_menu_count());
    lcd_db_show(lcd_dbs, smart_home_get_db_count());
    /* 状态栏: 自动/安防 */
    lcd_show_text(0, 224, g_state.auto_mode ? "AUTO ON " : "AUTO OFF",
                  g_state.auto_mode ? LCD_GREEN : LCD_GRAY, LCD_WHITE, 16, 0);
    lcd_show_text(96, 224, " MODE:",
                  LCD_BLACK, LCD_WHITE, 16, 0);
    lcd_show_text(160, 224, pir_mode_str(g_state.pir_mode),
                  LCD_BLUE, LCD_WHITE, 16, 0);
    /* 底部导航提示 */
    lcd_show_text(0,  204, "[UP] menu  [DOWN] enter  [L/R] page", LCD_RED, LCD_WHITE, 16, 0);
}

static void ui_render_security(void)
{
    lcd_fill(0, 0, LCD_W, LCD_H, LCD_WHITE);
    lcd_show_text(80, 4, "SECURITY - PIR", LCD_RED, LCD_WHITE, 24, 0);
    lcd_fill(0, 32, LCD_W, 34, LCD_GRAY);   /* 分隔线 y=32..33 */
    pir_status_t s = pir_get_status();
    lcd_show_text(0, 44, "  Mode: ",
                  LCD_BLACK, LCD_WHITE, 16, 0);
    lcd_show_text(96, 44, pir_mode_str(s.mode),
                  s.mode == PIR_MODE_ARM ? LCD_RED : LCD_BLUE,
                  LCD_WHITE, 16, 0);
    lcd_show_text(0, 72, "  Status: ",
                  LCD_BLACK, LCD_WHITE, 16, 0);
    lcd_show_text(96, 72, s.presence ? " PERSON! " : "  CLEAR  ",
                  s.presence ? LCD_RED : LCD_GREEN, LCD_WHITE, 16, 0);
    lcd_show_text(0, 100, "  Count: ",
                  LCD_BLACK, LCD_WHITE, 16, 0);
    char buf[32];
    sprintf(buf, "%u", (unsigned)s.trigger_count);
    lcd_show_text(96, 100, buf, LCD_MAGENTA, LCD_WHITE, 16, 0);
    /* 当前选中模式联动提示 */
    const char *hint[] = {
        "No action",
        "Light only",
        "Light+Beep alarm",
    };
    lcd_show_text(0, 128, "  Effect: ",
                  LCD_BLACK, LCD_WHITE, 16, 0);
    lcd_show_text(96, 128, hint[s.mode <= 2 ? s.mode : 0],
                  LCD_BLUE, LCD_WHITE, 16, 0);
    lcd_show_text(0, 190, "  [UP] mode  [L/R] page",
                  LCD_GRAY, LCD_WHITE, 16, 0);
    lcd_show_text(0, 210, "  [DOWN] enter ARM mode",
                  LCD_GRAY, LCD_WHITE, 16, 0);

    /* 整页渲染后, 同步 partial refresh 缓存 */
    sec_last_mode     = (int)s.mode;
    sec_last_presence = (int)s.presence;
    sec_last_count    = (int)s.trigger_count;
}

static void ui_render_auto(void)
{
    lcd_fill(0, 0, LCD_W, LCD_H, LCD_WHITE);
    lcd_show_text(120, 0, "AUTO", LCD_RED, LCD_WHITE, 24, 0);
    /* 顶部 1 行: 当前总开关 (auto_selected==0 时高亮) */
    bool mode_sel = (g_state.auto_editing && g_state.auto_selected == 0);
    uint16_t mode_fc = mode_sel ? LCD_RED : LCD_BLACK;
    lcd_show_text(0, 30, "  Mode:",
                  mode_fc, LCD_WHITE, 16, 0);
    if (mode_sel) {
        lcd_show_text(120, 30, "< ", LCD_RED, LCD_WHITE, 16, 0);
    }
    lcd_show_text(90, 30, g_state.auto_mode ? "[ ON ]" : "[ OFF ]",
                  g_state.auto_mode ? LCD_GREEN : LCD_GRAY,
                  LCD_WHITE, 16, 0);
    if (mode_sel) {
        lcd_show_text(240, 30, " >", LCD_RED, LCD_WHITE, 16, 0);
    }

    /* 5 行阈值 (idx=1..5), 选中行高亮 */
    const char *names[] = {
        "Light ON  ",
        "Light OFF ",
        "Fan Temp  ",
        "Fan Humi  ",
        "Fire PPM  ",
    };
    int vals[5] = {
        (int)g_state.thr.light_on,
        (int)g_state.thr.light_off,
        (int)g_state.thr.fan_temp,
        (int)g_state.thr.fan_humi,
        (int)g_state.thr.fire_ppm,
    };
    const char *units[] = { " lx", " lx", " C", " RH", " ppm" };
    for (int i = 0; i < 5; i++) {
        int y = 60 + i * 28;
        int row = i + 1;   /* 选中行 1..5 */
        bool sel = (g_state.auto_editing && g_state.auto_selected == row);
        uint16_t fc = sel ? LCD_RED : LCD_BLACK;
        lcd_show_text(0, y, names[i], fc, LCD_WHITE, 16, 0);
        char buf[32];
        if (sel) {
            lcd_show_text(120, y, "< ", LCD_RED, LCD_WHITE, 16, 0);
        }
        sprintf(buf, "%d%s", vals[i], units[i]);
        lcd_show_text(140, y, buf, fc, LCD_WHITE, 16, 0);
        if (sel) {
            lcd_show_text(240, y, " >", LCD_RED, LCD_WHITE, 16, 0);
        }
    }
    /* 底部提示 */
    lcd_show_text(0, 210, g_state.auto_editing
        ? "  [UP] sel  [L/R] val  [DOWN] end"
        : "  [L/R] page  [UP] sel  [DOWN] edit",
        LCD_GRAY, LCD_WHITE, 16, 0);

    /* 同步 partial refresh 缓存 */
    auto_last_auto_mode = g_state.auto_mode;
    auto_last_selected  = g_state.auto_selected;
    auto_last_editing   = g_state.auto_editing;
    auto_last_thr[0]    = (int)g_state.thr.light_on;
    auto_last_thr[1]    = (int)g_state.thr.light_off;
    auto_last_thr[2]    = (int)g_state.thr.fan_temp;
    auto_last_thr[3]    = (int)g_state.thr.fan_humi;
    auto_last_thr[4]    = (int)g_state.thr.fire_ppm;
}

static void ui_render_fire(void)
{
    lcd_fill(0, 0, LCD_W, LCD_H, LCD_RED);
    /* 32 字号大字: 提醒火灾 - 居中显示 */
    lcd_show_text(40, 50,  "检测到火灾",  LCD_WHITE, LCD_RED, 32, 0);
    lcd_show_text(40, 90,  "请核实情况",  LCD_WHITE, LCD_RED, 32, 0);
    lcd_show_text(50, 140, "请核实情况",  LCD_YELLOW, LCD_RED, 24, 0);
    /* 底部小字提示 - 因 16/24 字号字模里没有"管/员/可/关/等/秒/结/束",
     * 这里保持英文避免乱码. 中文字模中已有的字会在 32 字号大段自动出现. */
    lcd_show_text(0, 170, "  [UP] key to dismiss",
                     LCD_WHITE, LCD_RED, 16, 0);
    lcd_show_text(0, 195, "  [NFC] admin can dismiss",
                     LCD_WHITE, LCD_RED, 16, 0);
    lcd_show_text(0, 220, "  or wait 30s auto clear",
                     LCD_WHITE, LCD_RED, 16, 0);
}

void ui_render_current(void)
{
    /* 整屏刷前先画 wifi 一次 (在所有 ui_render_* 内部 lcd_fill 整屏擦之前画也行, 这里放后面保险) */
    switch (g_state.current_page) {
        case UI_PAGE_HOME:     ui_render_home();     break;
        case UI_PAGE_SECURITY: ui_render_security(); break;
        case UI_PAGE_AUTO:     ui_render_auto();     break;
        case UI_PAGE_FIRE:     ui_render_fire();     break;
        default: ui_render_home(); break;
    }
    /* 整屏渲染后再补一次 wifi 图标, 保证非 HOME 页 wifi 不消失 */
    if (g_state.current_page != UI_PAGE_FIRE) {
        ui_draw_wifi_icon();
    }
}

/* ============== navigation 路由 ==============
 * 可循环页面: HOME <-> SECURITY <-> AUTO <-> HOME
 * FIRE 是特殊页, 只在 fire_alarm 时进入, 不参与循环. */
static const ui_page_id_t s_nav[UI_PAGE_NAV_COUNT] = {
    UI_PAGE_HOME, UI_PAGE_SECURITY, UI_PAGE_AUTO
};

static int nav_index(ui_page_id_t p)
{
    for (int i = 0; i < UI_PAGE_NAV_COUNT; i++) {
        if (s_nav[i] == p) return i;
    }
    return 0;  /* FIRE 等异常页 -> 视为 HOME */
}

static ui_page_id_t nav_next(ui_page_id_t p)
{
    int i = nav_index(p);
    return s_nav[(i + 1) % UI_PAGE_NAV_COUNT];
}

static ui_page_id_t nav_prev(ui_page_id_t p)
{
    int i = nav_index(p);
    return s_nav[(i + UI_PAGE_NAV_COUNT - 1) % UI_PAGE_NAV_COUNT];
}

void ui_mark_dirty(void) { g_state.dirty = true; }

/* ============== 各页 partial refresh ============== */

/* SECURITY 局部刷新函数: 只刷 Status / Count / Effect 三行
 * (y=72/100/128), 标题/分隔线/左侧标签/底部提示保持不动.
 * 数值未变化则跳过, 减少 LCD 闪烁.
 * "上次值"缓存见文件顶部 sec_last_*. */
static void ui_refresh_security(void)
{
    pir_status_t s = pir_get_status();

    /* 数据变化检测: 任一变化 -> 只刷数据区 */
    if (s.mode == sec_last_mode &&
        (int)s.presence == sec_last_presence &&
        (int)s.trigger_count == sec_last_count) {
        return;  /* 一切未变, 不刷 */
    }

    /* ---------- Status 行 (y=72, x=96) ---------- */
    lcd_fill(96, 72, 240, 88, LCD_WHITE);
    lcd_show_text(96, 72, s.presence ? " PERSON! " : "  CLEAR  ",
                  s.presence ? LCD_RED : LCD_GREEN, LCD_WHITE, 16, 0);

    /* ---------- Count 行 (y=100, x=96) ---------- */
    lcd_fill(96, 100, 240, 116, LCD_WHITE);
    char buf[16];
    sprintf(buf, "%u", (unsigned)s.trigger_count);
    lcd_show_text(96, 100, buf, LCD_MAGENTA, LCD_WHITE, 16, 0);

    /* ---------- Effect 行 (y=128, x=96) ---------- */
    const char *hint[] = {
        "No action",
        "Light only",
        "Light+Beep alarm",
    };
    lcd_fill(96, 128, 240, 144, LCD_WHITE);
    lcd_show_text(96, 128, hint[s.mode <= 2 ? s.mode : 0],
                  LCD_BLUE, LCD_WHITE, 16, 0);

    /* ---------- Mode 行 (y=44, x=96) 也只刷值区 ---------- */
    if (s.mode != sec_last_mode) {
        lcd_fill(96, 44, 240, 60, LCD_WHITE);
        lcd_show_text(96, 44, pir_mode_str(s.mode),
                      s.mode == PIR_MODE_ARM ? LCD_RED : LCD_BLUE,
                      LCD_WHITE, 16, 0);
    }

    /* 更新缓存 */
    sec_last_mode     = (int)s.mode;
    sec_last_presence = (int)s.presence;
    sec_last_count    = (int)s.trigger_count;
}

/* AUTO: 局部刷新, 只重绘变化的行, 避免整屏刷屏 */
static void ui_refresh_auto(void)
{
    int cur_thr[5] = {
        (int)g_state.thr.light_on,
        (int)g_state.thr.light_off,
        (int)g_state.thr.fan_temp,
        (int)g_state.thr.fan_humi,
        (int)g_state.thr.fire_ppm,
    };

    /* 数据完全未变则跳过 */
    if (g_state.auto_mode == auto_last_auto_mode &&
        g_state.auto_selected == auto_last_selected &&
        g_state.auto_editing == auto_last_editing &&
        cur_thr[0] == auto_last_thr[0] &&
        cur_thr[1] == auto_last_thr[1] &&
        cur_thr[2] == auto_last_thr[2] &&
        cur_thr[3] == auto_last_thr[3] &&
        cur_thr[4] == auto_last_thr[4]) {
        return;
    }

    /* 页首刷 (auto_last_auto_mode 为 -1 表示首次) 或切页进入: 刷整屏 */
    if (auto_last_auto_mode < 0) {
        lcd_fill(0, 0, LCD_W, LCD_H, LCD_WHITE);
        lcd_show_text(120, 0, "AUTO", LCD_RED, LCD_WHITE, 24, 0);
    }

    const char *names[] = {"Light ON  ", "Light OFF ", "Fan Temp  ", "Fan Humi  ", "Fire PPM  "};
    const char *units[] = {" lx", " lx", " C", " RH", " ppm"};

    /* -- Mode 行 (y=30) -- */
    if (g_state.auto_mode != auto_last_auto_mode ||
        g_state.auto_selected != auto_last_selected ||
        auto_last_auto_mode < 0) {
        lcd_fill(0, 30, LCD_W, 48, LCD_WHITE);
        bool mode_sel = (g_state.auto_editing && g_state.auto_selected == 0);
        uint16_t mode_fc = mode_sel ? LCD_RED : LCD_BLACK;
        lcd_show_text(0, 30, "  Mode:", mode_fc, LCD_WHITE, 16, 0);
        if (mode_sel) {
            lcd_show_text(120, 30, "< ", LCD_RED, LCD_WHITE, 16, 0);
        }
        lcd_show_text(90, 30, g_state.auto_mode ? "[ ON ]" : "[ OFF ]",
                      g_state.auto_mode ? LCD_GREEN : LCD_GRAY, LCD_WHITE, 16, 0);
        if (mode_sel) {
            lcd_show_text(240, 30, " >", LCD_RED, LCD_WHITE, 16, 0);
        }
    }

    /* -- 阈值行 (y=60,88,116,144,172) -- */
    for (int i = 0; i < 5; i++) {
        int y = 60 + i * 28;
        int row = i + 1;
        bool was_sel = (auto_last_editing && auto_last_selected == row);
        bool is_sel  = (g_state.auto_editing && g_state.auto_selected == row);

        if (was_sel != is_sel || cur_thr[i] != auto_last_thr[i] || auto_last_auto_mode < 0) {
            lcd_fill(0, y, LCD_W, y + 20, LCD_WHITE);
            uint16_t fc = is_sel ? LCD_RED : LCD_BLACK;
            lcd_show_text(0, y, names[i], fc, LCD_WHITE, 16, 0);
            if (is_sel) {
                lcd_show_text(120, y, "< ", LCD_RED, LCD_WHITE, 16, 0);
            }
            char buf[32];
            sprintf(buf, "%d%s", cur_thr[i], units[i]);
            lcd_show_text(140, y, buf, fc, LCD_WHITE, 16, 0);
            if (is_sel) {
                lcd_show_text(240, y, " >", LCD_RED, LCD_WHITE, 16, 0);
            }
        }
    }

    /* -- 底部提示行 (y=210) -- */
    if (g_state.auto_selected != auto_last_selected ||
        g_state.auto_editing != auto_last_editing ||
        auto_last_auto_mode < 0) {
        lcd_fill(0, 210, LCD_W, 230, LCD_WHITE);
        lcd_show_text(0, 210, g_state.auto_editing
            ? "  [UP] sel  [L/R] val  [DOWN] end"
            : "  [L/R] page  [UP] sel  [DOWN] edit",
            LCD_GRAY, LCD_WHITE, 16, 0);
    }

    /* 更新缓存 */
    auto_last_auto_mode = g_state.auto_mode;
    auto_last_selected  = g_state.auto_selected;
    auto_last_editing   = g_state.auto_editing;
    auto_last_thr[0]    = cur_thr[0];
    auto_last_thr[1]    = cur_thr[1];
    auto_last_thr[2]    = cur_thr[2];
    auto_last_thr[3]    = cur_thr[3];
    auto_last_thr[4]    = cur_thr[4];
}

/* 局部刷新分发: 不擦整屏, 只重画当前页需要变的部分 */
void ui_refresh_partial(void)
{
    switch (g_state.current_page) {
        case UI_PAGE_HOME:     ui_refresh_dashboard_data(); break;
        case UI_PAGE_SECURITY: ui_refresh_security();       break;
        case UI_PAGE_AUTO:     ui_refresh_auto();           break;
        case UI_PAGE_FIRE:     break;  /* 火灾页不参与 partial */
        default: break;
    }
}

/* ============== 局部刷新(避免整屏刷新闪烁) ==============
 * 主循环每 1s 调一次, 但只刷变化的数据:
 *   1) 各 DB 的文字区(温度/湿度/光照/火灾数值)
 *   2) DB 图片仅在图标变化时重绘(温度高/正常, 火灾/无火灾)
 *   3) 底栏 AUTO/PIR MODE 状态仅在变化时重绘
 * 标题/菜单图片/WiFi 图标/静态文字不重绘. */

/* 主界面局部刷新缓存: 避免每秒重绘不变的图片和文字 */
static char  dash_last_text[4][16] = {{0}};
static const unsigned char *dash_last_img[4] = {NULL};
static int   dash_last_auto_mode = -1;
static int   dash_last_pir_mode  = -1;

void ui_refresh_dashboard_data(void)
{
    if (g_state.current_page != UI_PAGE_HOME) {
        return;
    }
    if (g_state.fire_alarm_active) {
        return;  /* 火灾页走 ui_render_fire() */
    }

    int db_count = smart_home_get_db_count();

    /* 只刷各 DB 的文字区, 图片仅在 icon 变化时重绘 */
    for (int i = 0; i < db_count; i++) {
        lcd_display_board_t *db = lcd_dbs[i];
        if (db == NULL) continue;

        int y  = db->base_y;
        int h  = (db->img.height > 0) ? db->img.height : 60;
        int tx = (db->text_x > 0) ? db->text_x : (db->base_x + db->img.width + 3);
        int ty = y + 8;  /* 文字 y 坐标, 与 lcd_db_draw 一致 */

        /* 图片变化时重绘图片区域 */
        if (db->img.img != dash_last_img[i]) {
            int w = (db->img.width > 0) ? db->img.width : 60;
            lcd_fill(db->base_x, y, db->base_x + w, y + h, LCD_WHITE);
            lcd_show_picture(db->base_x, y, db->img.width, db->img.height, db->img.img);
            dash_last_img[i] = db->img.img;
        }

        /* 文字变化时重绘文字区域 */
        if (strcmp(db->text.name, dash_last_text[i]) != 0) {
            lcd_fill(tx, y, tx + 110, y + h, LCD_WHITE);
            lcd_show_text(tx, ty, db->text.name, db->text.fc,
                          db->text.bc, db->text.font_size, 0);
            strcpy(dash_last_text[i], db->text.name);
        }
    }

    /* 底栏状态: 仅在 auto_mode 或 pir_mode 变化时重绘 */
    int cur_auto = (int)g_state.auto_mode;
    int cur_pir  = (int)g_state.pir_mode;
    if (cur_auto != dash_last_auto_mode || cur_pir != dash_last_pir_mode) {
        lcd_fill(0, 220, LCD_W, 240, LCD_WHITE);
        lcd_show_text(0, 224, g_state.auto_mode ? "AUTO ON " : "AUTO OFF",
                      g_state.auto_mode ? LCD_GREEN : LCD_GRAY, LCD_WHITE, 16, 0);
        lcd_show_text(96, 224, " MODE:",
                      LCD_BLACK, LCD_WHITE, 16, 0);
        lcd_show_text(160, 224, pir_mode_str(g_state.pir_mode),
                      LCD_BLUE, LCD_WHITE, 16, 0);
        dash_last_auto_mode = cur_auto;
        dash_last_pir_mode  = cur_pir;
    }
}

/* ============== 按键分发 (新逻辑) ==============
 * 规则:
 *   1) LEFT/RIGHT = 始终切页 (编辑态则调值)
 *   2) UP   = 向上选择参数 (到达最上后回到最下轮询)
 *   3) DOWN = 选中/确认 (进入编辑态, 再次按下退出编辑态)
 *   4) 编辑态: LEFT/RIGHT 调值, DOWN 退出编辑
 *   5) FIRE 是警报页, 任何按键忽略 (NFC 走 ui_on_event) */
static inline void set_pir_mode(int next)
{
    if (next < 0) next = 2;
    if (next > 2) next = 0;
    g_state.pir_mode = (pir_mode_t)next;
    g_state.thr.pir_mode = (uint8_t)next;
    pir_set_mode(g_state.pir_mode);
    /* 模式切换蜂鸣 */
    if (g_state.pir_mode == PIR_MODE_ARM) {
        beep_request(BEEP_ARM);
    } else if (g_state.pir_mode == PIR_MODE_DISARM) {
        beep_request(BEEP_DISARM);
    } else {
        beep_request(BEEP_CONFIRM);
    }
    g_state.dirty = true;
}

/* AUTO 页调值辅助函数 */
static void auto_adjust(int delta)
{
    int idx = g_state.auto_selected;
    if (idx == 0) {
        /* auto_mode toggle */
        if (delta != 0) {
            g_state.auto_mode = !g_state.auto_mode;
            lcd_set_auto_state(g_state.auto_mode);
        }
    } else if (idx == 1) {
        int v = (int)g_state.thr.light_on + delta * 5;
        if (v >= 5 && v <= 600) g_state.thr.light_on = (uint16_t)v;
    } else if (idx == 2) {
        int v = (int)g_state.thr.light_off + delta * 5;
        if (v >= 5 && v <= 1000) g_state.thr.light_off = (uint16_t)v;
    } else if (idx == 3) {
        int v = (int)g_state.thr.fan_temp + delta;
        if (v >= 5 && v <= 60) g_state.thr.fan_temp = (uint16_t)v;
    } else if (idx == 4) {
        int v = (int)g_state.thr.fan_humi + delta;
        if (v >= 5 && v <= 95) g_state.thr.fan_humi = (uint16_t)v;
    } else if (idx == 5) {
        int v = (int)g_state.thr.fire_ppm + delta * 10;
        if (v >= 50 && v <= 800) g_state.thr.fire_ppm = (uint16_t)v;
    }
    g_state.dirty = true;
}

void ui_on_key(uint8_t key_no)
{
    if (g_state.current_page == UI_PAGE_FIRE) {
        /* 火灾页: 按 UP 键立刻退出 (NFC 由 event_nfc_tap 触发) */
        if (key_no == KEY_UP) {
            ui_exit_fire_page();
        }
        return;
    }

    /* ===== LEFT/RIGHT: 编辑态调值, 否则切页 ===== */
    if (key_no == KEY_LEFT) {
        if (g_state.current_page == UI_PAGE_AUTO && g_state.auto_editing) {
            auto_adjust(-1);
        } else {
            g_state.current_page = nav_prev(g_state.current_page);
            g_state.dirty = true;
        }
        return;
    }
    if (key_no == KEY_RIGHT) {
        if (g_state.current_page == UI_PAGE_AUTO && g_state.auto_editing) {
            auto_adjust(+1);
        } else {
            g_state.current_page = nav_next(g_state.current_page);
            g_state.dirty = true;
        }
        return;
    }

    /* ===== HOME 页 ===== */
    if (g_state.current_page == UI_PAGE_HOME) {
        if (key_no == KEY_UP) {
            /* 向上切换菜单选中 (循环) */
            int new_idx = smart_home_get_menu_index() - 1;
            if (new_idx < 0) new_idx = smart_home_get_menu_count() - 1;
            smart_home_set_menu_index(new_idx);
            ui_refresh_home_menus();
            return;
        }
        if (key_no == KEY_DOWN) {
            /* 下键确认: 进入菜单 (开/关灯, 开/关风扇) */
            lcd_menu_entry(lcd_menus[smart_home_get_menu_index()]);
            ui_refresh_home_menus();
            g_state.dirty = true;
            return;
        }
        return;
    }

    /* ===== SECURITY 页 ===== */
    if (g_state.current_page == UI_PAGE_SECURITY) {
        if (key_no == KEY_UP) {
            /* 上键向上选择模式 (循环) */
            set_pir_mode((int)g_state.pir_mode - 1);
        } else if (key_no == KEY_DOWN) {
            /* 下键: 直接进入 ARM 布防模式 */
            set_pir_mode(PIR_MODE_ARM);
        }
        return;
    }

    /* ===== AUTO 页 ===== */
    if (g_state.current_page == UI_PAGE_AUTO) {
        if (key_no == KEY_UP) {
            /* 上键向上选择参数 (0→5→4→...→0 循环) */
            g_state.auto_selected--;
            if (g_state.auto_selected < 0) g_state.auto_selected = 5;
            g_state.dirty = true;
        } else if (key_no == KEY_DOWN) {
            /* 下键: 进入/退出编辑态 */
            g_state.auto_editing = !g_state.auto_editing;
            g_state.dirty = true;
        }
        return;
    }
}

/* ============== 事件 ============== */
void ui_enter_fire_page(void)
{
    if (g_state.fire_alarm_active) return;
    g_state.fire_alarm_active = true;
    g_state.current_page = UI_PAGE_FIRE;
    beep_request(BEEP_FIRE_ALARM);
    /* 关闭灯光防止误开 - 火灾时禁用 */
    light_set_state(false);
    lcd_set_light_state(false);
}

void ui_exit_fire_page(void)
{
    if (!g_state.fire_alarm_active) return;
    g_state.fire_alarm_active = false;
    g_state.current_page = UI_PAGE_HOME;
    beep_request(BEEP_FIRE_CLEAR);
    beep_silence();
}

void ui_on_event(event_info_t *evt)
{
    if (!evt) return;
    switch (evt->event) {
        case event_nfc_tap:
            if (g_state.current_page == UI_PAGE_FIRE) {
                /* 火灾页 NFC 触发: 直接退出, 不再校验 token */
                ui_exit_fire_page();
            }
            break;
        default: break;
    }
}
