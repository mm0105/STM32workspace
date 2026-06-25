# 智慧仓储安全联动控制系统

> 基于 **通晓开发板（RK2206 / Huawei LiteOS）** 与 **华为云 IoTDA** 的嵌入式 IoT 综合项目  
> 覆盖：多任务 RTOS 编程、I2C/SPI/UART/ADC/PWM/GPIO 中断驱动、事件驱动架构、状态机、业务级硬件抽象、MQTT 物联网云端协议

---

## 一、项目简介

在仓库场景下，温湿度异常、光照不足、可燃气体泄漏都会带来货物损失和安全风险。本项目**从零实现**了一套安全联动 IoT 系统：

- **环境感知**：SHT30 温湿度、BH1750 光照、MQ2 可燃气体、HC-SR501 人体感应
- **本地控制**：4 页 TFT LCD UI、5 向 ADC 按键、SU-03T 离线语音、PWM 蜂鸣器（9 种业务音型）
- **执行器**：RGB 灯（照明/夜灯/报警）、PWM 直流电机（风扇/排烟）
- **安全联动**：PIR 三模式（撤防/夜灯/布防）、NT3H NFC 管理员卡双路径认证、火灾全流程闭环
- **云端**：WiFi STA + MQTT 上报 9 项属性至华为云 IoTDA，支持设备影子远程控制

### 一句话定位

> 多线程事件驱动 + 业务级硬件抽象 + 多模式安全联动 + 物联网云端接入 的嵌入式 IoT 全栈项目，可直接作为嵌入式 / IoT 岗的简历亮点。

---

## 二、技术栈

| 类别 | 选型 |
|---|---|
| MCU | Rockchip **RK2206**（ARM Cortex-A32 双核，OpenHarmony 衍生） |
| RTOS | **Huawei LiteOS**（任务、队列、信号量、软件定时器） |
| 总线协议 | I2C0（**SHT30 / BH1750 / NT3H** 三设备共挂）、SPI（TFT LCD）、UART2（SU-03T） |
| 通信 | WiFi STA + **MQTT 5.0 / 3.1.1**（华为云 IoTDA） |
| 驱动 | 自实现 PWM（蜂鸣器/电机）、ADC 采样（MQ2 / 按键分压）、GPIO 中断（PIR / NFC FD） |
| 软件设计 | 事件驱动 + 状态机 + 业务级抽象 + 双路径容错 |
| 构建 | GN / Ninja（OpenHarmony 标准构建） |
| 语言 | C99（无 STL 依赖，适合裸机 / RTOS 环境） |

---

## 三、核心亮点（面试官重点看）

### 3.1 多线程事件驱动架构

8 个 LiteOS 任务（按优先级 / 职责分层），统一通过 `event_info_t` 队列通信，**无共享变量竞争**。

| 线程 | 优先级 | 职责 |
|---|---|---|
| `smart_home_thread` | 24 | 主循环：事件分发 + 4 路传感器采集 + 火灾判断 + 自动模式 + MQTT 上报 + UI 刷新 |
| `iot_thread` | 24 | WiFi STA 连接与重连、MQTT 收发 |
| `adc_key_thread` | 24 | 100ms 软定时器轮询 5 向按键 + 组合键识别 |
| `su_03t_thread` | 24 | UART2 接收 SU-03T 离线语音指令 |
| `device_read` | 24 | SHT30 / BH1750 周期采集 |
| `beep_thread` ⭐ | 18 | 消费 `event_beep_request`，异步播放 9 种业务音型 |
| `pir_thread` ⭐ | 22 | 1s 心跳 + 3s 冷却 + 5s 夜灯延时 |
| `nfc_fd_isr` ⭐ | - | FD 引脚下降沿中断 → 发 `event_nfc_tap` |

**事件类型**（`smart_home_event.h`）：

```c
typedef enum {
    event_key_press     = 1,
    event_iot_cmd       = 2,
    event_su03t         = 3,
    event_beep_request  = 4,
    event_pir_trigger   = 5,
    event_nfc_tap       = 6,
    event_fire_alarm    = 7,
    event_fire_clear    = 8,
    event_timer_tick    = 9,
    event_uart_recv     = 10,
} event_type_t;
```

### 3.2 业务级硬件抽象（分层设计的范本）

**蜂鸣器**：业务方只调 `beep_request(BEEP_FIRE_ALARM)`，**不接触** PWM 频率/占空比/时长。

| 音型 | 频率 / 时长 | 业务含义 |
|---|---|---|
| `BEEP_KEY_CLICK`  | 3 kHz × 30ms | 按键短哔 |
| `BEEP_CONFIRM`    | 短-短-短 | 确认音 |
| `BEEP_FIRE_ALARM` | 2~2.7 kHz × 6 次 | 火灾急促 |
| `BEEP_INTRUDER`   | 880 Hz × 1.6s | 入侵长鸣 |
| `BEEP_GREETING`   | 上行双音 | 迎宾叮咚 |
| `BEEP_ARM` / `BEEP_DISARM` | 低音组合 | 布防 / 撤防 |
| `BEEP_NFC_OK`     | 880 Hz × 100ms | NFC 验证通过 |
| `BEEP_FIRE_CLEAR` | 下行三音 | 火灾解除 |

由 `g_patterns[]` 表驱动，**新增音型只需扩表，无需改业务代码**。

### 3.3 NFC 双路径容错

NT3H1101 NFC 标签的 FD 引脚与 SESSION 寄存器都能反映"有 RF 场"：

- **主路径**：FD 引脚（`GPIO0_PB0`）下降沿中断 → ISR 发 `event_nfc_tap`（响应即时）  
- **降级路径**：I2C 读 SESSION 寄存器 (0xFE) 的 `FIELD_PRESENT` 位（FD 引脚未飞线时自动启用，4s 轮询）

### 3.4 PIR 三模式安全联动 + 3s 冷却防抖

HC-SR501 持续高电平期间会持续触发，本设计加 **3s 业务冷却** + **5s 夜灯延时关**：

| 模式 | PIR 触发后 | 退出策略 |
|---|---|---|
| `DISARM` | 不响应 | — |
| `NIGHT`  | 开灯 + 5s 延时关 | 倒计时归零自动关 |
| `ARM`    | 蜂鸣 `BEEP_INTRUDER` + 开灯 | 灯交 `light_menu` |

### 3.5 4 页 UI 状态机 + 局部按需刷新

`ui_pages.c` 把 HOME / SECURITY / AUTO / FIRE 四页状态机集中管理：

- **切页**时 `ui_render_current` 整屏重绘  
- **状态变化**时按 `dirty` 标志位 `ui_refresh_partial` 局部刷新  
- 避免 32~200 px 区域每秒全刷导致 SPI 长时间阻塞与 LCD 闪烁

### 3.6 火灾全流程闭环

```
MQ2 ppm > 阈值（默认 200） 持续 1s
    └─→ fire_state = FIRE_LOCKED
        └─→ LCD 强制切到全屏红色 FIRE 页（48 号字"检测到火灾，请核实情况"）
        └─→ 发 event_fire_alarm + event_beep_request(BEEP_FIRE_ALARM)
        └─→ RGB 灯 0.5s 周期红闪
        └─→ MQTT 上报 fireStatus=ON
        └─→ 屏蔽所有 LEFT/RIGHT 切页

解除（满足任一）：
  A. ppm < 阈值 - 50 持续 3s       → 自动解除
  B. NFC 刷卡匹配 ADMIN_OK token   → 蜂鸣 BEEP_FIRE_CLEAR + 退出 FIRE 页
```

### 3.7 MQ2 冷启动容错

MQ2 气体传感器上电需要预热 5 分钟以上，本设计：

- 上电 5s 后自动 **R0 校准**  
- 电压异常、NaN/Inf 结果统一置 0  
- 避免冷启动误报

---

## 四、硬件引脚分配

| 主控引脚 | 接口 | 用途 |
|---|---|---|
| SPI (MOSI/CLK/CS/DC/RST) | TFT LCD (ST7789) | 4 页 UI 屏幕显示 |
| I2C0 @ 0x44 / 0x23 / 0x55 | SHT30 / BH1750 / NT3H | 三设备共挂总线 |
| ADC4 | MQ2 AO | 可燃气体浓度 |
| ADC7 | 按键分压电阻网络 | 5 向按键（UP/DOWN/LEFT/RIGHT/OK） |
| GPIO | RGB LED (R/G/B) | 照明 + 夜灯 + 报警红闪 |
| PWM6 (GPIO0_C6) | 直流电机 | 风扇 PWM 调速 |
| **PWM5 (GPIO0_C5)** | **有源蜂鸣器** | **9 种业务级音型** |
| **GPIO0_PA3** | **HC-SR501 OUT** | **人体感应上升沿中断** |
| **GPIO0_PB0** | **NT3H FD** | **NFC 双路径触发** |
| UART2 (TX/RX) | SU-03T | 离线语音 |
| SDIO / UART | WiFi 模组 | STA 上云 |

---

## 五、软件架构

### 5.1 主循环伪代码

```c
void smart_home_thread(void *arg)
{
    /* 1. 初始化所有外设 + 等待 MQ2 预热 5s */
    i2c_dev_init();   lcd_dev_init();
    motor_dev_init(); light_dev_init();
    su03t_init();     mq2_dev_init();
    beep_init();      pir_init();     nfc_app_init();
    LOS_Msleep(5000); mq2_ppm_calibration();

    /* 2. 4 页 UI 初始渲染 */
    ui_state_init();
    ui_render_current();

    /* 3. 主循环: 事件驱动 + 周期采集 */
    while (1) {
        event_info_t evt;
        smart_home_event_wait(&evt, 1000);

        /* 派发按键 / 语音 / IoT / PIR / NFC / 火灾事件 */
        if (evt.event == event_key_press)   ui_on_key(evt.data.key_no);
        if (evt.event == event_iot_cmd)     smart_home_iot_cmd_process(...);
        if (evt.event == event_su03t)       smart_home_su03t_cmd_process(...);
        if (evt.event == event_nfc_tap)     ui_on_event(&evt);
        if (evt.event == event_pir_trigger) { st->dirty = true; }
        if (evt.event == event_fire_alarm)  ui_enter_fire_page();
        if (evt.event == event_fire_clear)  ui_exit_fire_page();

        /* 读 4 路传感器 */
        sht30_read_data(&temp, &humi);
        bh1750_read_data(&lum);
        ppm = get_mq2_ppm();

        /* 火灾判断 + 自动模式 */
        if (ppm > thr.fire_ppm) ui_enter_fire_page();
        if (ui.auto_mode && !ui.fire_alarm_active) {
            if (lum < thr.light_on)  light_set_state(true);
            if (temp > thr.fan_temp) motor_set_state(true);
        }

        /* MQTT 上报 + UI 局部刷新 */
        if (mqtt_is_connected()) send_msg_to_mqtt(&iot_data);
        if (ui.dirty)            ui_refresh_partial();
    }
}
```

### 5.2 4 页 UI 设计

| Page | 名称 | 主要内容 | 翻页 |
|---|---|---|---|
| 0 | **HOME** 主控 | 灯光 / 风扇 / 温湿度 / 光照 / 火灾面板 | ← → |
| 1 | **SECURITY** 安防 | PIR 模式（DISARM/NIGHT/ARM）+ 状态 + 触发计数 | ← → |
| 2 | **AUTO** 自动 | 开关 + 5 阈值行（light_on/off、fan_temp/humi、fire_ppm） | ← → |
| 3 | **FIRE** 火灾 | 全屏红字 + 请刷 NFC 提示 | **不参与循环**，NFC 退出 |

---

## 六、华为云 IoTDA 接入

### 6.1 产品模型属性（9 项）

| 属性 | 类型 | 说明 |
|---|---|---|
| `illumination` | string | 光照（lx） |
| `temperature`  | string | 温度（℃） |
| `humidity`     | string | 湿度（%RH） |
| `gas`          | string | 可燃气体（ppm） |
| `fireStatus`   | string (ON/OFF) | 火灾状态 |
| `motorStatus`  | string (ON/OFF) | 电机状态 |
| `lightStatus`  | string (ON/OFF) | 灯光状态 |
| `autoStatus`   | string (ON/OFF) | 自动模式状态 |
| `pirMode`      | string (DISARM/NIGHT/ARM) | PIR 模式 |

### 6.2 修改 `iot.c` 凭据

```c
#define HOST_ADDR           "xxx.st1.iotda-device.cn-north-4.myhuaweicloud.com"
#define CLIENT_ID           "..."
#define DEVICE_ID           "..."
#define MQTT_DEVICES_PWD    "..."
```

### 6.3 WiFi 凭据（`iot_smart_home_example.c` 顶部）

```c
#define ROUTE_SSID      "your-wifi-ssid"
#define ROUTE_PASSWORD  "your-wifi-password"
```

---

## 七、目录结构

```
mjs/                                       # 项目根（git 仓库根）
├── README.md                              # ← 本文件
├── BUILD.gn                               # OpenHarmony GN 构建脚本
├── iot_smart_home_example.c               # 程序入口（创建 8 任务）
├── include/                               # 公共头文件
│   ├── beep.h            # 蜂鸣器 API
│   ├── pir.h             # PIR API + 模式枚举
│   ├── nfc.h / nfc_app.h # NT3H NFC + 管理员认证
│   ├── ui_pages.h        # 4 页 UI 状态机
│   ├── smart_home_event.h / smart_home.h
│   ├── drv_light.h / drv_motor.h / drv_sensors.h
│   ├── lcd.h / lcd_font.h / picture.h
│   ├── adc_key.h / mq2.h / su_03t.h / iot.h / components.h
│   └── NT3H.h
├── src/                                   # 驱动 + 业务实现
│   ├── beep.c             ⭐ 9 种业务音型
│   ├── pir.c              ⭐ PIR 三模式 + 3s 冷却
│   ├── nfc.c / nfc_app.c  ⭐ NFC 双路径
│   ├── ui_pages.c         ⭐ 4 页 UI 状态机
│   ├── smart_home.c / smart_home_event.c
│   ├── lcd.c / picture.c / components.c
│   ├── drv_light.c / drv_motor.c / drv_sensors.c
│   ├── adc_key.c / mq2.c / su_03t.c / iot.c
│   └── NT3H.c
└── mjs/                                   # 详细设计资料（设计报告 / 流程图 / 用户手册）
    ├── 报告内容.md        # 完整设计报告（5 万字）
    ├── 需求文档.md        # 需求规格
    ├── 流程图.md          # 11 张 Mermaid 流程图 + 9 种音型表
    ├── USER_MANUAL.md     # 用户使用手册（含故障排查）
    ├── 电路图图片/        # 硬件电路素材
    ├── 图片/              # 流程图源文件 (.drawio / .mmd)
    └── 报告生成预览/      # 报告 docx 预览与流程图 PNG
```

---

## 八、编译与运行

### 8.1 构建系统接入

在 `vendor/isoftstone/rk2206/sample/BUILD.gn` 中添加：

```python
"./e1_iot_smart_home_hwiot:iot_smart_home_example",
```

在 `device/rockchip/rk2206/sdk_liteos/Makefile` 中添加：

```python
hardware_LIBS = -lhal_iothardware -lhardware -liot_smart_home_example,
```

### 8.2 编译

```bash
# 在 OpenHarmony 源码根目录
hb set    # 选择 ipcamera_hispark_taurus / iot_smart_home
hb build -b iot_smart_home_example -f
```

### 8.3 烧录

使用 RKDevTool / `upgrade_tool` 烧录 `Hi3861_loader_ram.bin` 与 `OHOS_Image.bin` 至通晓开发板。

### 8.4 串口日志关键字

| 日志 | 含义 |
|---|---|
| `[beep] init ok` | 蜂鸣器线程已创建 |
| `[beep] IoTPwmInit failed` | PWM5 初始化失败（蜂鸣器将无声） |
| `[pir] init ok` | PIR 初始化完成 |
| `[nfc_app] admin token matched` | NFC 管理员卡验证通过 |
| `[fire] alarm on` / `[fire] clear` | 火灾触发 / 解除 |
| `[main] ppm=...` | 主循环每秒打印的传感器数据 |

---

## 九、关键文件快速索引

| 模块 | 入口 | 关键 API |
|---|---|---|
| 蜂鸣器 | [src/beep.c](src/beep.c) | `beep_init()` / `beep_request(BEEP_TYPE)` |
| PIR | [src/pir.c](src/pir.c) | `pir_init()` / `pir_set_mode()` / `pir_get_presence()` |
| NFC | [src/nfc_app.c](src/nfc_app.c) | `nfc_app_init()` / `nfc_app_check_admin()` |
| 4 页 UI | [src/ui_pages.c](src/ui_pages.c) | `ui_state_init()` / `ui_render_current()` / `ui_refresh_partial()` / `ui_enter_fire_page()` |
| 事件总线 | [src/smart_home_event.c](src/smart_home_event.c) | `smart_home_event_send()` / `smart_home_event_wait()` |
| 主线程 | [iot_smart_home_example.c](iot_smart_home_example.c) | `smart_home_thread()` / `iot_thread()` |
| 火灾 + 自动模式 | [src/smart_home.c](src/smart_home.c) | `smart_home_iot_cmd_process()` / `smart_home_su03t_cmd_process()` |
| MQTT | [src/iot.c](src/iot.c) | `mqtt_init()` / `send_msg_to_mqtt()` / `wait_message()` |

---

## 十、简历项目描述模板

> **智慧仓储安全联动控制系统**（通晓开发板 / RK2206 / LiteOS / 华为云 IoTDA）  
> 独立设计并实现一套嵌入式 IoT 安全联动系统，覆盖 **7 路外设**（SPI LCD / I2C SHT30+BH1750+NT3H / ADC MQ2+按键 / PWM 蜂鸣器+电机 / GPIO PIR+FD / UART 语音 / WiFi MQTT）。  
> 基于 **LiteOS 多任务 + 事件队列** 构建 8 线程事件驱动架构；设计 **4 页 UI 状态机** 与按需局部刷新策略；实现 **蜂鸣器业务级音型抽象**（9 种 `beep_type_t` 枚举）、**PIR 三模式安全联动**（撤防/夜灯/布防）、**NFC 管理员卡双路径认证**（FD 中断 + I2C SESSION 寄存器降级）。  
> 通过华为云 IoTDA 上报 9 项属性，支持设备影子远程控制；MQ2 冷启动校准 + NaN/Inf 容错避免误报。

---

## 十一、详细文档索引

项目设计报告、流程图、用户手册全部放在 `mjs/mjs/` 子目录：

| 文档 | 路径 | 内容 |
|---|---|---|
| 设计报告 | [mjs/报告内容.md](mjs/报告内容.md) | 5 万字完整设计报告（任务、硬件、程序、测试、总结） |
| 需求规格 | [mjs/需求文档.md](mjs/需求文档.md) | 项目代号、硬件资源、任务划分、8 项功能需求、验收标准 |
| 流程图 | [mjs/流程图.md](mjs/流程图.md) | 11 张 Mermaid 流程图 + 9 种音型表 + 避坑表 |
| 用户手册 | [mjs/USER_MANUAL.md](mjs/USER_MANUAL.md) | 5 页 UI 详解 + 8 项参数说明 + 故障排查 + 串口日志 |
| 报告 docx | [mjs/缪俊燊_程哲_智慧仓库环境监测系统_大作业设计报告.docx](mjs/缪俊燊_程哲_智慧仓库环境监测系统_大作业设计报告.docx) | Word 版完整设计报告（可直接打印） |
| 流程图源文件 | [mjs/图片/](mjs/图片/) | `.drawio` / `.mmd` 流程图源文件 |
| 报告生成预览 | [mjs/报告生成预览/](mjs/报告生成预览/) | 报告 docx 预览 + 流程图 PNG |

---

## 十二、后续可扩展方向

- 移植到 **RT-Thread / FreeRTOS**（验证架构的平台无关性）  
- 接入 **OneNet / 阿里云 IoT**（验证云端 SDK 适配能力）  
- 增加 **LoRa / NB-IoT** 通信（脱离 WiFi 限制）  
- 增加 **OTA 升级** 通道（IAP 远程烧录）  
- 抽离为通用 **事件驱动嵌入式框架**（模板化 8 任务 + 事件队列）

---

## 十三、版权与致谢

- 基线版本：`e1_iot_smart_home_hwiot` 智慧仓库例程（iSoftStone Education）  
- 平台：通晓开发板（RK2206）、Huawei LiteOS、OpenHarmony  
- 云端：华为云 IoTDA  
- 作者：缪俊燊（智慧仓库环境监测系统 大作业）
