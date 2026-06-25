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

#include "smart_home.h"

#include <stdio.h>
#include <stdbool.h>

#include "iot_errno.h"

#include "iot_pwm.h"
#include "iot_gpio.h"
#include "su_03t.h"
#include "iot.h"
#include "lcd.h"
#include "picture.h"
#include "adc_key.h"
#include "components.h"
#include "lcd.h"
#include "string.h"

static bool auto_state = false;
/* 注: 旧版为 static, 导致 ui_pages.c 渲染主页时取不到 wifi 状态, 移除 static */
bool network_state = false;

void light_menu_entry(lcd_menu_t *menu);
void fan_menu_entry(lcd_menu_t *menu);

/* 风扇菜单的数据初始化*/
lcd_menu_t fan_menu={
    .img={
        .img=img_fan_off,
        .height=64,
        .width=64,
    },
    .is_selected=false,
    .text={
        .fc=LCD_MAGENTA,
        .bc=LCD_WHITE,
        .font_size=24,
        .name="风扇关",
    },
    .enterFunc=fan_menu_entry,
    .exitFunc=NULL,
    .base_x=100,
    .base_y=36,
};

/* 照明灯的菜单初始化数据*/
lcd_menu_t light_menu={
    .img={
        .img= img_light_off,
        .height=64,
        .width=64,
    },
    .is_selected=false,
    .text={
        .fc=LCD_MAGENTA,
        .bc=LCD_WHITE,
        .font_size=24,
        .name="灯光关",
    },
    .base_x=20,
    .base_y=36,
    .enterFunc=light_menu_entry,
    .exitFunc=NULL,
};

/* 温度面板的初始化数据*/
lcd_display_board_t temp_db={
    .img={
        .img= img_temp_normal,
        .height=48,
        .width=48,
    },
    .text={
        .fc=LCD_MAGENTA,
        .bc=LCD_WHITE,
        .font_size=24,
        .name="25.5C",
    },
    .base_x=180,
    .base_y=36,
};

/* 湿度面板的初始化数据*/
lcd_display_board_t humi_db={
    .img={
        .img= img_humi,
        .height=48,
        .width=48,
    },
    .text={
        .fc=LCD_MAGENTA,
        .bc=LCD_WHITE,
        .font_size=24,
        .name="25.5C",
    },
    .base_x=180,
    .base_y=90,
};

/* 亮度面板的初始化数据*/
lcd_display_board_t lum_db={
    .img={
        .img= img_lum,
        .height=48,
        .width=48,
    },
    .text={
        .fc=LCD_MAGENTA,
        .bc=LCD_WHITE,
        .font_size=24,
        .name="1234Lx",
    },
    .base_x=180,
    .base_y=144,
};
/* 火灾（有害气体）面板的初始化数据，位置在原logo处（左下角）*/
lcd_display_board_t fire_db={
    .img={
        .img= gImage_hzmfs,     /* 默认显示"无火灾"图标 */
        .height=52,
        .width=43,
    },
    .text={
        .fc=LCD_GREEN,
        .bc=LCD_WHITE,
        .font_size=24,
        .name="0ppm",
    },
    .base_x=10,
    .base_y=144,
    /* 文字固定X坐标: 不随图片宽度(40/43)变化, 防止切图时文字左移3像素
       导致旧的"!"位置没被新空格覆盖而留红字 */
    .text_x=10+48+3,
};
/* 所有的面板集合数组,方便遍历查询*/
lcd_display_board_t *lcd_dbs[] ={&temp_db,&humi_db,&lum_db,&fire_db};
/* 所有的菜单集合数组,方便遍历查询*/
lcd_menu_t *lcd_menus[] = {&light_menu,&fan_menu};
/* 菜单的个数*/
static int lcd_menu_number =  sizeof(lcd_menus)/sizeof(lcd_menu_t *);
/* 菜单的当前选中索引,在数组中的位置*/
static int menu_select_index = 0;

int  smart_home_get_menu_index(void) { return menu_select_index; }
void smart_home_set_menu_index(int idx)
{
    if (idx < 0) idx = 0;
    if (idx >= lcd_menu_number) idx = lcd_menu_number - 1;
    menu_select_index = idx;
}
int  smart_home_get_menu_count(void) { return lcd_menu_number; }
int  smart_home_get_db_count(void)
{
    return (int)(sizeof(lcd_dbs) / sizeof(lcd_display_board_t *));
}

//菜单左和右的处理,需要考虑菜单个数的边界
void lcd_menu_selected_move_left()
{
    if(menu_select_index > 0){
        menu_select_index--;
    }
}

void lcd_menu_selected_move_right()
{
    if(menu_select_index <lcd_menu_number-1){
        menu_select_index++;
    }
}

/**
 * @brief 照明灯菜单按下确认按键
 * 
 * @param menu 
 */
void light_menu_entry(lcd_menu_t *menu)
{
    int light_state = get_light_state();
    if(light_state)
    {
        light_set_state(false);
        lcd_set_light_state(false);
    }else{
        light_set_state(true);
        lcd_set_light_state(true);
    }
}
/**
 * @brief  风扇菜单按下确认按键
 * 
 * @param menu 
 */
void fan_menu_entry(lcd_menu_t *menu)
{
   int motor_state = get_motor_state();
   if(motor_state)
   {
       motor_set_state(false);
       lcd_set_motor_state(false);
   }else{
       motor_set_state(true);
       lcd_set_motor_state(true);
   }
}


/***************************************************************
* 函数名称: lcd_dev_init
* 说    明: lcd初始化
* 参    数: 无
* 返 回 值: 无
***************************************************************/
void lcd_dev_init(void)
{
    lcd_init();
    lcd_fill(0, 0, LCD_W, LCD_H, LCD_WHITE);
}

/**
 * @brief 按键处理函数
 * 
 * @param key_no 按键号
 */
void smart_home_key_process(int key_no)
{
    printf("smart_home_key_process:%d\n",key_no);
    if(key_no == KEY_UP){

    }else if(key_no == KEY_DOWN){
        lcd_menu_entry(lcd_menus[menu_select_index]);

    }else if(key_no == KEY_LEFT){
        
        lcd_menu_selected_move_left();
    }else if(key_no == KEY_RIGHT){
        lcd_menu_selected_move_right();
    }
}
/**
 * @brief 物联网的指令处理函数
 * 
 * @param iot_cmd iot的指令
 */
void smart_home_iot_cmd_process(int iot_cmd)
{
    switch (iot_cmd)
    {
        case IOT_CMD_LIGHT_ON:
            light_set_state(true);
            lcd_set_light_state(true);
            break;
        case IOT_CMD_LIGHT_OFF:
            light_set_state(false);
            lcd_set_light_state(false);
            break;
        case IOT_CMD_MOTOR_ON:
            motor_set_state(true);
            lcd_set_motor_state(true);
            break;
        case IOT_CMD_MOTOR_OFF:
            motor_set_state(false);
            lcd_set_motor_state(false);
            break;
    }
}

/**
 * @brief 语音管家发出的指令
 * 
 * @param su03t_cmd 语音管家的指令
 */
void smart_home_su03t_cmd_process(int su03t_cmd)
{
    switch (su03t_cmd)
    {
        case light_state_on:
            light_set_state(true);
            lcd_set_light_state(true);
            break;
        case light_state_off:
            light_set_state(false);
            lcd_set_light_state(false);
            break;
        case motor_state_on:
            motor_set_state(true);
            lcd_set_motor_state(true);
            break;
        case motor_state_off:
            motor_set_state(false);
            lcd_set_motor_state(false);
            break;
        case temperature_get:
        {
            double temp,humi;

            sht30_read_data(&temp, &humi);
            su03t_send_double_msg(1, temp);
        }
            
            break;
        case humidity_get:
        {
            double temp,humi;
            sht30_read_data(&temp, &humi);

            su03t_send_double_msg(2, humi);
        }
            break;
        case illumination_get:
        {
            double lum;
            bh1750_read_data(&lum);
            su03t_send_double_msg(3, lum);
        }
            
            break;
        default:
            break;
    }
}


/***************************************************************
* 函数名称: lcd_load_ui
* 说    明: 加载lcd ui
* 参    数: 无
* 返 回 值: 无
***************************************************************/
void lcd_show_ui(void)
{

    lcd_show_text(96, 0, "SMART WH", LCD_RED, LCD_WHITE, 16, 0);
    lcd_show_picture(280,0, 32,32, network_state? img_wifi_on : img_wifi_off);

    lcd_menu_update(lcd_menus ,lcd_menu_number,menu_select_index);

    lcd_menu_show(lcd_menus ,  lcd_menu_number);
    lcd_db_show(lcd_dbs, sizeof(lcd_dbs)/sizeof(lcd_display_board_t *));
}

/***************************************************************
* 函数名称: lcd_set_temperature
* 说    明: 设置温度显示
* 参    数: double temperature 温度
* 返 回 值: 无
***************************************************************/
void lcd_set_temperature(double temperature)
{
    sprintf(temp_db.text.name, "%.01f℃ ", temperature);
    /* 对温度做高温和正常的区分*/
    if(temperature > 35)
    {
        temp_db.text.fc = LCD_RED;
        temp_db.img.img = img_temp_high;
    }
    else
    {
       temp_db.text.fc = LCD_MAGENTA;
       temp_db.img.img = img_temp_normal;
    }
}

/***************************************************************
* 函数名称: lcd_set_humidity
* 说    明: 设置湿度显示
* 参    数: double humidity 湿度
* 返 回 值: 无
***************************************************************/
void lcd_set_humidity(double humidity)
{
    sprintf(humi_db.text.name, "%.01f%% ", humidity);

}

/***************************************************************
* 函数名称: lcd_set_illumination
* 说    明: 设置光照强度显示
* 参    数: double illumination 光照强度
* 返 回 值: 无
***************************************************************/
void lcd_set_illumination(double illumination)
{
    sprintf(lum_db.text.name, "%.01fLx ", illumination);

}

/***************************************************************
* 函数名称: lcd_set_fire_state
* 说    明: 设置火灾（有害气体）显示, 始终显示当前浓度ppm值
*           并根据阈值切换"火灾发生/无火灾"图标
*           模仿温湿度面板的处理: 文本尾部补空格, 保证两种状态
*           字符位数一致, 切回"无火灾"时自动清掉红色'!'
* 参    数: double ppm 有害气体浓度
* 返 回 值: 无
***************************************************************/
void lcd_set_fire_state(double ppm)
{
    /* ppm 阈值可按现场情况调整
       文本固定 6 字符, 报警/正常都用同样的"%.0fppm  "格式(2 个尾空格),
       报警/正常的差异仅靠颜色 (红/绿) + 图标 区分,
       这样切回正常时不会出现任何残影 */
    if(ppm > 200.0)
    {
        sprintf(fire_db.text.name, "%.0fppm  ", ppm);
        fire_db.text.fc = LCD_RED;
        fire_db.img.img  = gImage_hzfs;     /* 火灾发生图标 */
        fire_db.img.width  = 40;            /* 40x48 */
        fire_db.img.height = 48;
    }
    else
    {
        sprintf(fire_db.text.name, "%.0fppm  ", ppm);
        fire_db.text.fc = LCD_GREEN;
        fire_db.img.img  = gImage_hzmfs;    /* 无火灾图标 */
        fire_db.img.width  = 43;            /* 43x52 */
        fire_db.img.height = 52;
    }
}

void lcd_set_network_state(int state){
    /* 状态没变则不画, 避免主循环 1s 重复刷同一个图标浪费 SPI 带宽 */
    if ((bool)state == network_state) {
        return;
    }
    network_state = state;
    /* 旧版只改全局变量, LCD 永远不会画 wifi 图标 -> 切到主页时 wifi 消失.
     * 这里在 (x=280,y=0,32x32) 局部刷新一次, 不擦整屏. */
    lcd_fill(280, 0, 312, 32, LCD_WHITE);
    lcd_show_picture(280, 0, 32, 32,
                     network_state ? img_wifi_on : img_wifi_off);
}

/***************************************************************
* 函数名称: lcd_set_light_state
* 说    明: 设置灯状态显示
* 参    数: bool state true：显示"打开" false：显示"关闭"
* 返 回 值: 无
***************************************************************/
void lcd_set_light_state(bool state)
{
    
    strcpy(light_menu.text.name,state? "灯光开" :"灯光关");
    light_menu.img.img = state? img_light_on : img_light_off;
}

/***************************************************************
* 函数名称: lcd_set_motor_state
* 说    明: 设置电机状态显示
* 参    数: bool state true：显示"打开" false：显示"关闭"
* 返 回 值: 无
***************************************************************/
void lcd_set_motor_state(bool state)
{

    strcpy(fan_menu.text.name,state? "风扇开" :"风扇关");
    fan_menu.img.img = state? img_fan_on : img_fan_off;

}

/***************************************************************
* 函数名称: lcd_set_auto_state
* 说    明: 设置自动模式状态显示
* 参    数: bool state true：显示"打开" false：显示"关闭"
* 返 回 值: 无
***************************************************************/
void lcd_set_auto_state(bool state)
{
    /* 旧版函数体是空的, 切换 auto_mode 后 LCD 不刷新, 看不到状态变化.
     * 这里直接重画底部状态栏. 颜色: 开=绿, 关=灰. */
    lcd_fill(0, 222, 96, 240, LCD_WHITE);
    lcd_show_text(0, 224, state ? "AUTO ON " : "AUTO OFF",
                  state ? LCD_GREEN : LCD_GRAY, LCD_WHITE, 16, 0);
}
