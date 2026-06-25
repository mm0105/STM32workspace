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
#include <stdbool.h>

#include "los_task.h"
#include "ohos_init.h"
#include "cmsis_os.h"
#include "config_network.h"
#include "smart_home.h"
#include "smart_home_event.h"
#include "su_03t.h"
#include "iot.h"
#include "iot_gpio.h"
#include "iot_errno.h"
#include "lcd.h"
#include "picture.h"
#include "adc_key.h"
#include "mq2.h"

#include "beep.h"
#include "pir.h"
#include "nfc_app.h"
#include "ui_pages.h"

#define ROUTE_SSID      "your-wifi-ssid"      // WiFi账号
#define ROUTE_PASSWORD  "your-wifi-password"  // WiFi密码

#define MSG_QUEUE_LENGTH                                16
#define BUFFER_LEN                                      50


/***************************************************************
 * 函数名称: iot_thread
 * 说    明: iot线程
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void iot_thread(void *args) {
  uint8_t mac_address[12] = {0x00, 0xdc, 0xb6, 0x90, 0x01, 0x00,0};

  char ssid[32]=ROUTE_SSID;
  char password[32]=ROUTE_PASSWORD;
  char mac_addr[32]={0};

  FlashDeinit();
  FlashInit();

  VendorSet(VENDOR_ID_WIFI_MODE, "STA", 3); // 配置为Wifi STA模式
  VendorSet(VENDOR_ID_MAC, mac_address, 6); // 多人同时做该实验，请修改各自不同的WiFi MAC地址
  VendorSet(VENDOR_ID_WIFI_ROUTE_SSID, ssid, sizeof(ssid));
  VendorSet(VENDOR_ID_WIFI_ROUTE_PASSWD, password,sizeof(password));

reconnect:
  SetWifiModeOff();
  int ret = SetWifiModeOn();
  if(ret != 0){
    printf("wifi connect failed,please check wifi config and the AP!\n");
    return;
  }
  mqtt_init();

  while (1) {
    if (!wait_message()) {
      goto reconnect;
    }
    LOS_Msleep(1);
  }
}


/***************************************************************
 * 函数名称: smart_home_thread
 * 说    明: 智慧家居主线程(集成 6 页 UI / 自动模式 / 火灾 / PIR / NFC)
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void smart_home_thread(void *arg)
{
    double temp = 0, humi = 0, lum = 0, ppm = 0;
    e_iot_data iot_data = {0};

    i2c_dev_init();
    lcd_dev_init();
    motor_dev_init();
    light_dev_init();
    su03t_init();
    mq2_dev_init();

    /* UI / 蜂鸣器 / PIR / NFC 初始化 */
    ui_state_init();
    /* beep/pir/nfc_app 由 APP_FEATURE_INIT 自启 */

    printf("Waiting for MQ2 warm-up...\n");
    LOS_Msleep(5000);
    mq2_ppm_calibration();

    ui_render_current();

    /* 火灾页自动结束计时(秒) */
    static int fire_alarm_ticks = 0;
    /* 备份上一次 auto_mode 状态, 用于检测退出自动模式时关灯/关电机 */
    static bool last_auto_mode = false;
    /* 主循环: 1s 周期做一次 sensor 读取 + 自动模式 + MQTT.
     * 关键: 整屏刷一次约 0.4s, 整循环要留 < 1s, 否则周期堆栈累积导致死机. */
    while (1)
    {
        ui_state_t *st = ui_get_state();
        /* 备份事件处理前的状态, 用于决定刷屏策略 */
        ui_page_id_t prev_page = st->current_page;

        event_info_t event_info = {0};
        int ret = smart_home_event_wait(&event_info, 1000);
        if (ret == LOS_OK) {
            switch (event_info.event) {
                case event_key_press:
                {
                    uint8_t k = event_info.data.key_no;
                    ui_on_key(k);
                    break;
                }
                case event_iot_cmd:
                    smart_home_iot_cmd_process(event_info.data.iot_data);
                    break;
                case event_su03t:
                    smart_home_su03t_cmd_process(event_info.data.su03t_data);
                    break;
                case event_pir_trigger:
                    /* PIR 状态变化 -> 标记 dirty, 触发 SECURITY 页 partial 刷新
                     * (pir.c 内部已更新 g_pir_presence / g_pir_count) */
                    st->dirty = true;
                    break;
                case event_nfc_tap:
                    ui_on_event(&event_info);
                    break;
                case event_fire_alarm:
                    ui_enter_fire_page();
                    fire_alarm_ticks = 0;
                    break;
                case event_fire_clear:
                    ui_exit_fire_page();
                    break;
                default: break;
            }
        }

        /* 读传感器 */
        sht30_read_data(&temp, &humi);
        bh1750_read_data(&lum);
        ppm = get_mq2_ppm();
        printf("[main] ppm=%.2f, temp=%.2f, humi=%.2f, lum=%.2f\n",
               ppm, temp, humi, lum);

        lcd_set_illumination(lum);
        lcd_set_temperature(temp);
        lcd_set_humidity(humi);
        lcd_set_fire_state(ppm);

        /* 火灾检测: ppm 超过阈值即进入火灾页 (直接调用, 不经过队列) */
        if (!st->fire_alarm_active && ppm > (double)st->thr.fire_ppm) {
            ui_enter_fire_page();
            fire_alarm_ticks = 0;
        }
        /* 火灾自动结束: 30 秒内 ppm 持续 < 阈值则退出 */
        if (st->fire_alarm_active) {
            fire_alarm_ticks++;
            if (ppm < (double)st->thr.fire_ppm) {
                /* 留 30s 给现场处理 */
                if (fire_alarm_ticks > 30) {
                    ui_exit_fire_page();
                }
            } else {
                fire_alarm_ticks = 0;
            }
            /* 火灾页内主动探测 NFC(兼容 FD 引脚未飞线的情况):
             * 多重检测: 1) nfc_app 完整检测 2) 直接读 FD 引脚电平 */
            static int fire_poll_div = 0;
            if ((++fire_poll_div & 0x01) == 0) {  /* 每 2 个主循环 ~ 2s 探测一次 */
                int exit_fire = 0;

                /* 方式 1: nfc_app 完整检测(含 I2C SESSION 和 FD 中断标志) */
                nfc_tap_result_t r = nfc_app_check_admin();
                if (r == NFC_TAP_OK) {
                    exit_fire = 1;
                } else if (r == NFC_TAP_FAIL) {
                    printf("[fire] non-admin card tapped, ignore\n");
                }

                /* 方式 2: 直接读 FD 引脚电平(低电平 = 有 NFC 场) */
                if (!exit_fire) {
                    IotGpioValue fd_level = IOT_GPIO_VALUE1;
                    if (IoTGpioGetInputVal(NFC_FD_GPIO, &fd_level) == IOT_SUCCESS
                        && fd_level == IOT_GPIO_VALUE0) {
                        printf("[fire] fd pin low, exit\n");
                        exit_fire = 1;
                    }
                }

                if (exit_fire) {
                    ui_exit_fire_page();
                }
            }
        }

        /* 检测自动模式退出: 从 true 变为 false 时关闭灯和电机 */
        if (last_auto_mode && !st->auto_mode) {
            light_set_state(false);
            lcd_set_light_state(false);
            motor_set_state(false);
            lcd_set_motor_state(false);
        }
        last_auto_mode = st->auto_mode;

        /* 自动模式: 光照 / 温度 / 湿度 阈值控制灯和风扇 */
        if (st->auto_mode && !st->fire_alarm_active) {
            bool want_light = false;
            bool want_fan   = false;
            if (lum < (double)st->thr.light_on) {
                want_light = true;
            } else if (lum > (double)st->thr.light_off) {
                want_light = false;
            }
            if (temp > (double)st->thr.fan_temp ||
                humi > (double)st->thr.fan_humi) {
                want_fan = true;
            }
            if (want_light != (bool)get_light_state()) {
                light_set_state(want_light);
                lcd_set_light_state(want_light);
            }
            if (want_fan != (bool)get_motor_state()) {
                motor_set_state(want_fan);
                lcd_set_motor_state(want_fan);
            }
        }

        if (mqtt_is_connected()) {
            iot_data.illumination = lum;
            iot_data.temperature  = temp;
            iot_data.humidity     = humi;
            iot_data.gas_ppm      = ppm;
            iot_data.light_state  = get_light_state();
            iot_data.motor_state  = get_motor_state();
            iot_data.auto_state   = st->auto_mode;
            iot_data.fire_state   = st->fire_alarm_active;
            send_msg_to_mqtt(&iot_data);
            lcd_set_network_state(true);
        } else {
            lcd_set_network_state(false);
        }

        /* ============== 按需刷新策略 ==============
         * 1) 切页 (page changed) -> 整屏刷 (ui_render_current)
         * 2) 同页状态变化 (dirty) -> 局部刷 (ui_refresh_partial, 不擦整屏)
         * 3) HOME 页周期 1s       -> 刷传感器数据 (局部, 不擦整屏)
         * 4) 火灾页/其它非 HOME 页周期 1s: 跳过, 省 SPI
         * 注意: 必须在所有可能改 current_page / dirty 的逻辑执行完之后再判断. */
        bool page_changed = (st->current_page != prev_page);
        if (page_changed) {
            ui_render_current();
        } else if (st->dirty) {
            ui_refresh_partial();
        } else if (st->current_page == UI_PAGE_HOME && !st->fire_alarm_active) {
            /* 周期 1s 刷 HOME 传感器数据 */
            ui_refresh_dashboard_data();
        }
        st->dirty = false;
    }
}

/***************************************************************
 * 函数名称: device_read_thraed
 * 说    明: 设备读取线程
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
// void device_read_thraed(void *arg)
// {
//     double read_data[3] = {0};

//     i2c_dev_init();

//     while(1)
//     {
//         bh1750_read_data(&read_data[0]);
//         sht30_read_data(&read_data[1]);
//         LOS_QueueWrite(m_msg_queue, (void *)&read_data, sizeof(read_data), LOS_WAIT_FOREVER);
//         LOS_QueueWrite(m_su03_msg_queue, (void *)&read_data, sizeof(read_data), LOS_WAIT_FOREVER);
//         LOS_Msleep(500);
//     }
// }

/***************************************************************
 * 函数名称: iot_smart_home_example
 * 说    明: 开机自启动调用函数
 * 参    数: 无
 * 返 回 值: 无
 ***************************************************************/
void iot_smart_home_example()
{
    unsigned int thread_id_1;
    unsigned int thread_id_2;
    unsigned int thread_id_3;
    TSK_INIT_PARAM_S task_1 = {0};
    TSK_INIT_PARAM_S task_2 = {0};
    TSK_INIT_PARAM_S task_3 = {0};
    unsigned int ret = LOS_OK;
    
    smart_home_event_init();
    
    // ret = LOS_QueueCreate("su03_queue", MSG_QUEUE_LENGTH, &m_su03_msg_queue, 0, BUFFER_LEN);
    // if (ret != LOS_OK)
    // {
    //     printf("Falied to create Message Queue ret:0x%x\n", ret);
    //     return;
    // }

    task_1.pfnTaskEntry = (TSK_ENTRY_FUNC)smart_home_thread;
    task_1.uwStackSize = 8192;  /* 加大栈: lcd_fill 整屏 76800 次 lcd_wr_data + 多次 lcd_show_text 嵌套, 旧 2048 极易栈溢出 */
    task_1.pcName = "smart home thread";
    task_1.usTaskPrio = 24;
    
    ret = LOS_TaskCreate(&thread_id_1, &task_1);
    if (ret != LOS_OK)
    {
        printf("Falied to create task ret:0x%x\n", ret);
        return;
    }

    task_2.pfnTaskEntry = (TSK_ENTRY_FUNC)adc_key_thread;
    task_2.uwStackSize = 2048;
    task_2.pcName = "key thread";
    task_2.usTaskPrio = 24;
    ret = LOS_TaskCreate(&thread_id_2, &task_2);
    if (ret != LOS_OK)
    {
        printf("Falied to create task ret:0x%x\n", ret);
        return;
    }

    task_3.pfnTaskEntry = (TSK_ENTRY_FUNC)iot_thread;
    task_3.uwStackSize = 20480*5;
    task_3.pcName = "iot thread";
    task_3.usTaskPrio = 24;
    ret = LOS_TaskCreate(&thread_id_3, &task_3);
    if (ret != LOS_OK)
    {
        printf("Falied to create task ret:0x%x\n", ret);
        return;
    }
}

APP_FEATURE_INIT(iot_smart_home_example);
