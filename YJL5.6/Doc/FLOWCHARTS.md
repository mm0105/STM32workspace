# 篮球比赛记分牌 - 程序流程图

> 规范: 开始/结束用 `([文字])`、判断用 `{文字}`、操作用 `[文字]`、`flowchart` 顶格。
> `Z([返回])` 为函数出口，`while(1)` 死循环无结束节点。

---

## 模块列表

| 编号 | 模块名称 | 文件 | 包含函数 |
|------|----------|------|----------|
| 1 | 主程序模块 | main.c | `main()`, `GameTimer_Init()` |
| 2 | OLED 显示模块 | main.c | `Display_*()` 系列函数 |
| 3 | 按键模块 | Key.c | `Key_Init()`, `Key_Scan()`, `Key_IsPressed()` |
| 4 | 蜂鸣器模块 | Buzzer.c | `Buzzer_Init()`, `Buzzer_Beep()`, `Buzzer_Off()` |
| 5 | 定时器中断模块 | stm32f10x_it.c | `TIM2_IRQHandler()` |

---

## 图 1 - 主程序流程图

```mermaid
flowchart TD
    A("主程序开始") --> B["硬件初始化 + 关闭板载七段数码管"]
    B --> C["外设初始化: OLED / Key / Buzzer / GameTimer"]
    C --> D["Display_Welcome + Display_TimeSelect"]
    D --> E{"调用哪个状态分支?"}
    E -->|IDLE| F["按键 1~4 选时间 + Game_StartQuarter + 蜂鸣器响"]
    E -->|GAMEOVER| G["任意键 → Game_Reset 回到时间选择"]
    E -->|PAUSED| H["任意键 → 恢复计时 进入 PLAYING"]
    E -->|PLAYING| I["KEY1/3 队加 1 分 / KEY2/4 短按 +2 分 长按 +3 分"]
    F --> J["OLED 区域刷新 + Key_Scan 扫描按键"]
    G --> J
    H --> J
    I --> J
    J --> Z("返回主循环")
    Z --> E
```

---

## 图 2 - 显示模块流程图

```mermaid
flowchart TD
    A([开始]) --> B{调用哪个函数?}
    B -->|"Display_Welcome"| C["OLED_Clear~显示: Basketball Score / STM32F103C8T6 / Scoreboard V1.0~延时 1500ms"]
    C --> Z
    B -->|"Display_TimeSelect"| D["OLED_Clear~显示时间选择界面~K1:5s / K2:5m / K3:15m / K4:30m~清空 g_refresh_flags"]
    D --> Z
    B -->|"Display_ScoreA / B"| E["格式化为 A: XXX 或 B: XXX~OLED_ShowString 写 Line 1 或 Line 2"]
    E --> Z
    B -->|"Display_Time"| F["格式化为 QN MM:SS~OLED_ShowString 写 Line 3"]
    F --> Z
    B -->|"Display_Status"| G{"g_timer_running 等于 1?"}
    G -->|是| H["Line 4 显示: 1/3:+1 2/4:+2 L3"]
    G -->|否| I["Line 4 显示: PAUSE-ANY KEY  "]
    H --> Z
    I --> Z
    B -->|"Display_GameOver"| J["OLED_Clear~显示 GAME OVER~显示胜者~显示比分~显示 ANY KEY=RESTART"]
    J --> Z
    Z([返回])
```

---

## 图 3 - 蜂鸣器模块流程图

```mermaid
flowchart TD
    A("开始") --> B{"调用哪个函数?"}
    B -->|"Buzzer_Init"| C["使能 GPIOB / TIM3 时钟~PB0 配置复用推挽输出"]
    C --> D["TIM3 10KHz PWM 初始化~CH3 初始占空比 0 关闭蜂鸣器"]
    D --> Z
    B -->|"Buzzer_Beep"| E["TIM_SetCompare3 占空比 50% 鸣叫"]
    E --> F["Delay_ms 阻塞延时~TIM_SetCompare3 占空比 0 关闭"]
    F --> Z
    B -->|"Buzzer_Off"| G["TIM_SetCompare3 占空比 0 关闭蜂鸣器"]
    G --> Z
    Z("返回")
```

---

## 图 4 - 按键模块流程图

```mermaid
flowchart TD
    A([开始]) --> B{调用哪个函数?}
    B -->|"Key_Init"| C["使能 GPIOA / AFIO 时钟~PA12~PA15 配置上拉输入~GPIO_PinRemapConfig 禁用 JTAG 和 SWD"]
    C --> Z
    B -->|"Key_Scan"| D["遍历 KEY1~KEY4~检查 key_released 标志与引脚电平"]
    D --> E{"key_released 等于 0~且引脚回到高电平?"}
    E -->|是| F["重置 key_released = 1 允许再次触发"]
    F --> H["继续扫描下一键"]
    E -->|否| G{"引脚低电平 且 key_released = 1?"}
    G -->|是| I["Delay_ms 20ms 消抖~二次确认低电平~key_released 置 0~return KEY_PRESS"]
    G -->|否| J["无有效按键~return KEY_NONE"]
    I --> Z
    J --> Z
    H --> Z
    B -->|"Key_IsPressed"| K["根据 key_num 参数~GPIO_ReadInputDataBit 读取原始电平~返回 1(按下) 或 0(松开)"]
    K --> Z
    Z([返回])
```

---

## 图 5 - 定时器中断模块流程图

```mermaid
flowchart TD
    A("开始") --> B{"调用哪个函数?"}
    B -->|"TIM2_IRQHandler"| C["g_sys_tick 自增 1 供长按检测使用"]
    C --> D{"g_timer_running 等于 1?"}
    D -->|否| Z
    D -->|是| E{"g_game_sec 大于 0?"}
    E -->|是| F["g_game_sec 减 1~g_refresh_flags 置 RF_TIME"]
    F --> Z
    E -->|否| G{"g_game_min 大于 0?"}
    G -->|是| H["g_game_min 减 1~g_game_sec 设为 59~g_refresh_flags 置 RF_TIME"]
    H --> Z
    G -->|否| I["g_timer_running 置 0~g_game_state = PAUSED~g_refresh_flags 置 RF_TIME | RF_STATUS"]
    I --> Z
    Z("返回")
```

---

## 图清单

| 编号 | 流程图名称 | 节点数 | 说明 |
|------|-----------|--------|------|
| 1 | 主程序流程图 | 11 节点 | 初始化 + 状态机分支调度 |
| 2 | OLED 显示模块流程图 | 13 节点 | 6 个 Display_* 函数分支 |
| 3 | 蜂鸣器模块流程图 | 9 节点 | Init / Beep / Off 三函数分支 |
| 4 | 按键模块流程图 | 12 节点 | Init / Scan / IsPressed 三函数分支 |
| 5 | 定时器中断模块流程图 | 10 节点 | 1ms 计时 + 倒计时 + 暂停切换 |
