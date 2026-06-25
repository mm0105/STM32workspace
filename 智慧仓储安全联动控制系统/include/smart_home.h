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

#ifndef __SMART_HOME_H__
#define __SMART_HOME_H__

#include <stdint.h>
#include <stdbool.h>
#include "components.h"


void i2c_dev_init(void);
void bh1750_read_data(double *dat);
void sht30_read_data(double *temp, double *humi);

// void light_dev_init(void);
// void light_set_pwm(unsigned int duty);
// void light_set_state(bool state);

// void motor_dev_init(void);
// void motor_set_pwm(unsigned int duty);
// void motor_set_state(bool state);

void lcd_dev_init(void);
void lcd_show_ui(void);
void lcd_set_temperature(double temperature);
void lcd_set_humidity(double humidity);
void lcd_set_illumination(double illumination);
void lcd_set_fire_state(double ppm);
void lcd_set_light_state(bool state);
void lcd_set_motor_state(bool state);
void lcd_set_auto_state(bool state);
void lcd_set_network_state(int state);

/* 全局 WiFi 状态: ui_pages.c 渲染主页时直接读, lcd_set_network_state 写 */
extern bool network_state;

/* 主页菜单索引读写(ui_pages.c 切换页面时使用) */
int  smart_home_get_menu_index(void);
void smart_home_set_menu_index(int idx);
int  smart_home_get_menu_count(void);
int  smart_home_get_db_count(void);

/* 主页菜单/仪表盘全局数组(ui_pages.c 渲染主控页时使用) */
extern lcd_menu_t          *lcd_menus[];
extern lcd_display_board_t *lcd_dbs[];

void smart_home_su03t_cmd_process(int su03t_cmd);
void smart_home_iot_cmd_process(int iot_cmd);
void smart_home_key_press_process(uint8_t key_no);

#endif
