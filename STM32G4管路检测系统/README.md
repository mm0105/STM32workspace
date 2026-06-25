# STM32G4 管路检测系统

> 基于 STM32G4 的小型工业级管路监控 + 闭环控制项目
> 涵盖：压力采集、流量测量、PWM 调压、LCD 菜单、按键状态机、LED 报警

---

## 1. 项目简介

本工程实现一个 **管路检测与闭环控制系统**。系统实时采集管路压力与流量，并根据用户设定的目标值（手动目标占空比 / 自动目标压力）调节 PWM 输出占空比，进而驱动下游比例阀/调压回路。同时根据采集量判断 **堵塞 / 泄漏 / 传感器异常** 三类故障并通过 LED 指示。

适合作为嵌入式面试作品集的一个中型项目，演示：

- STM32 HAL + CubeMX 工程组织
- 多定时器分工（输入捕获 / PWM / 1ms 任务调度）
- ADC + 传感器零点校准
- 按键短按/长按状态机
- LCD 分页 UI 与局部刷新
- 74HC595 串行转并行 LED 驱动
- 手动 / 自动双模式控制算法

---

## 2. 硬件平台

| 项目 | 配置 |
| --- | --- |
| MCU | STM32G431 系列（Cortex-M4F，硬件 FPU，主频 80 MHz） |
| 晶振 | HSE 外部晶振，PLL 锁到 80 MHz（PLLM=3 / PLLN=20 / PLLP=2） |
| 工程类型 | STM32CubeMX 生成（`17403250.ioc`），HAL 库 |
| 显示屏 | 字符型 LCD（白底/黑底可配，调用 `LCD_DisplayStringLine`） |
| LED 驱动 | 74HC595 移位寄存器（4 位，低电平点亮） |
| 压力传感器 | 0~3.3 V 模拟量输出（默认假设量程 0~10 Bar） |
| 流量传感器 | 脉冲式频率输出（每升约 200 脉冲，可由 `flow.c` 系数调整） |
| 比例阀/调压 | PWM 5%~95% 控制 + 一路 50 Hz 舵机信号作辅助指示 |

---

## 3. 系统架构

```
                      ┌────────────────────┐
                      │      app.c         │
                      │  app_init / run    │
                      └────────┬───────────┘
                               │
   ┌──────────┬──────────┬────┴─────┬──────────┬──────────┐
   ▼          ▼          ▼          ▼          ▼          ▼
 key.c    lcd_ui.c    pwm.c      flow.c   adc_sensor  led.c
 按键     3 页 UI     PWM 输出   流量采集   压力采集  74HC595
   │          │          │          │          │          │
   └──────────┴──────────┴────┬─────┴──────────┴──────────┘
                             ▼
                  ┌────────────────────┐
                  │   timer_task.c     │
                  │ TIM3 1ms 分派任务  │
                  └─────────┬──────────┘
                            ▼
                 HAL_TIM_PeriodElapsedCallback (main.c)
```

**任务调度周期（基于 TIM3 1ms 中断）**：

| 周期 | 任务 | 实现 |
| --- | --- | --- |
| 每次中断（1 ms） | 按键扫描 | `key_scan()` |
| 50 ms | LED 状态判定 + 74HC595 刷新 | `led_tick()` |
| 100 ms | ADC 压力采样 + 流量累计 | `adc_get_pressure()` + `flow_accumulate_q()` |
| 1000 ms | PWM 斜坡更新 | `pwm_ramp_tick()` |

---

## 4. 模块说明

### 4.1 入口层（`Core/Src/main.c` + `app.c`）
- `main()`：HAL 初始化 → 6 个外设 MX 初始化 → `app_init()` → 主循环 `app_run()`
- `HAL_TIM_PeriodElapsedCallback`：TIM3 中断转发到 `timer_task_dispatch`，TIM16 输入捕获转发到 `flow_ic_capture`
- `app_init()`：统一关闭 LED → LCD 黑底白字 → ADC 内部偏移校准 → 启动 PWM（初始 5%）→ 启动 TIM16 输入捕获 → 启动 TIM3 1ms 中断
- `app_run()`：主循环只做两件事：按键处理 + LCD 刷新

### 4.2 按键（`key.c`）
- 4 个按键：B1 切页、B2 模式/选项、B3 加、B4 减
- 状态机：`0 空闲 → 1 按下确认（消抖）→ 2 持续按下`
- 长按阈值：`KEY_LONG_MS = 2000` ms
- 长按功能（仅 MAIN 页）：B3 长按 = 压力零点校准；B4 长按 = 累计流量清零

### 4.3 流量（`flow.c`）
- TIM16 通道 1 上升沿输入捕获，计数时钟 1 MHz（80 MHz / 80 预分频）
- 两次上升沿差值 → 频率 `fre1`（Hz）→ 瞬时流量 `F = fre1 / 200`（L/min）
- 异常频段：`fre1 > 8000` 视为传感器异常并触发 LD1
- 低流量：`fre1 < 800` 时 `F = 0`（视为无流量，避免抖动）
- 累计：`F × 0.1s / 60` 累加到 `Q_acc`，满 1 L 才让 `Q_value` 整数 +1

### 4.4 压力（`adc_sensor.c`）
- ADC2 单端采样，参考电压 3.3 V，12 bit（4096）
- 上电调用 `HAL_ADCEx_Calibration_Start` 内部偏移校准
- 零点偏置 `Voffset` 参与线性映射：`P = (V - Voffset) × 10 / (3.3 - Voffset)`，结果限幅 0~10 Bar
- 用户可在 MAIN 页长按 B3 把当前电压存为新 `Voffset`

### 4.5 PWM 输出（`pwm.c`）
- TIM2_CH2（PA1）：5%~95% 占空比直接写 CCR
- TIM17_CH1（PA7）：50 Hz 舵机信号，`CCR = 500 + (D - 5) × 200 / 9`（500~2500 µs）
- **斜坡策略**（每秒 ±1%）：防止比例阀/舵机瞬时跳变造成水锤

### 4.6 LCD 界面（`lcd_ui.c`）
- 3 个页面：`PAGE_MAIN / PAGE_OUTPUT / PAGE_PARA`
- 切换页面时整页重绘；MAIN 页每秒 10 次仅刷新数据行（Line3~Line8），避免标题闪烁
- MAIN 页数据行：模式 / P / F / Q / D / V

### 4.7 LED / 报警（`led.c`）
- 74HC595 串行转并行：先全部熄灭、锁存、置目标位、再次锁存
- 报警 LED 含义：

| 灯 | 触发条件 | 含义 |
| --- | --- | --- |
| LD1 | `fre1 > 8000` Hz | 流量传感器异常 |
| LD2 | `F < FL` 且 `D > DH`，持续 2 s | 管路堵塞 |
| LD3 | `F > FH` 且 `P < PL`，持续 2 s | 管路泄漏 |
| LD4 | `tar_changed == 1` | 系统处于动态调节中 |

---

## 5. UI 与按键交互

### 5.1 MAIN（监控）
```
       MAIN

   M=MAN
   P=1.2BAR
   F=18.0L/M
   Q=123L
   D=35%
   V=1.65V
```
- B1 → 切到 OUTP
- B2 → 切换 MAN / AUTO
- B3 长按 2 s → 零点校准（把当前电压写入 `Voffset`）
- B4 长按 2 s → 累计流量清零

### 5.2 OUTP（输出配置）
```
       OUTP

   TarD=35%
   TarP=3.0BAR
```
- B1 → 切到 PARA
- B2 → 切换 TarD / TarP
- B3 / B4 → 加 / 减（TarD 步长 5%，TarP 步长 0.5 Bar）
- 退出时若本次在 OUTP 改过值，会触发 LD4 表示"目标已生效"

### 5.3 PARA（报警阈值）
```
       PARA

   FH=20.0L/M
   FL=10.0L/M
   PL=1.0BAR
   DH=65%
```
- B1 → 回到 MAIN
- B2 → 切换 FH / FL / PL / DH
- B3 / B4 → 加 / 减

---

## 6. 控制算法

### 6.1 手动模式（MAN）
每秒 1% 斜坡：`D_actual` 朝 `TarD_value` 逼近（±1%/s），到目标后清 `tar_changed`。

### 6.2 自动模式（AUTO）
- 死区：`P ∈ [TarP - 0.5, TarP + 0.5] Bar` 内不动作，清 `tar_changed`
- 偏低：`P < TarP - 0.5` → `D_actual++`（限幅 95）
- 偏高：`P > TarP + 0.5` → `D_actual--`（限幅 5）
- 同样保持 1%/s 斜坡

### 6.3 报警判定
所有报警用 "**条件成立 + 持续 2 s**" 触发，避免瞬时抖动误报。

---

## 7. 文件目录

```
STM32G4管路检测系统/
├── 17403250.ioc                # CubeMX 工程
├── Core/
│   ├── Inc/                    # 头文件
│   │   ├── main.h
│   │   ├── app.h
│   │   ├── settings.h          # 全局变量/阈值/页面/周期宏
│   │   ├── key.h
│   │   ├── lcd_ui.h
│   │   ├── led.h
│   │   ├── pwm.h
│   │   ├── flow.h
│   │   ├── adc_sensor.h
│   │   ├── timer_task.h
│   │   └── stm32g4xx_it.h
│   └── Src/                    # 源文件
│       ├── main.c
│       ├── app.c
│       ├── settings.c
│       ├── key.c
│       ├── lcd_ui.c
│       ├── led.c
│       ├── pwm.c
│       ├── flow.c
│       ├── adc_sensor.c
│       ├── timer_task.c
│       └── ...
└── Drivers/                    # HAL / CMSIS（CubeMX 生成，不要手改）
```

---

## 8. 关键引脚与定时器

| 资源 | 用途 | 备注 |
| --- | --- | --- |
| TIM2_CH2 / PA1 | 主控 PWM 输出 | 5%~95% 占空比 |
| TIM3 | 1 ms 任务调度 | 全局时基 |
| TIM16_CH1 | 流量脉冲输入捕获 | 上升沿触发，1 MHz 计数 |
| TIM17_CH1 / PA7 | 舵机 PWM | 50 Hz，500~2500 µs |
| ADC2 | 压力传感器电压采集 | PA?（以 CubeMX 为准） |
| PB0 / PB1 / PB2 / PA0 | B1~B4 按键 | 内部上拉，按下低电平 |
| PD2 | 74HC595 锁存 | 上升沿锁存 |
| PC8~PC11 | 74HC595 数据 | 低 4 位对应 LD1~LD4 |

> 详细 IO 见 `17403250.ioc`；修改后请用 CubeMX 重新生成 `gpio.c/tim.c/adc.c`。

---

## 9. HAL 库与 CubeMX 工程

### 9.1 工程基本信息

| 项目 | 值 |
| --- | --- |
| 芯片 | **STM32G431RBT6**（Cortex-M4F + FPU） |
| 封装 | LQFP64 |
| 工具链 | STM32CubeMX 6.9.0（`MxCube.Version=6.9.0`，`MxDb.Version=DB.6.0.90`） |
| HAL 版本 | STM32G4xx HAL Driver（CubeMX 6.9 自带） |
| 调试接口 | SWD（`PA13` = SWDIO，`PA14` = SWCLK） |
| 库管理 | HAL + CMSIS（`Drivers/STM32G4xx_HAL_Driver/` + `Drivers/CMSIS/`） |

### 9.2 CubeMX 里实际开了哪些外设

`17403250.ioc` 中 `Mcu.IPNb=7`：

| IP | 作用 | CubeMX 配置要点 |
| --- | --- | --- |
| `RCC` | 时钟 | HSE 外部晶振 → PLL → 80 MHz SYSCLK |
| `SYS` | 系统 | 调试 = Serial Wire；时基 = SysTick |
| `NVIC` | 中断 | 优先级分组 `NVIC_PRIORITYGROUP_4` |
| `ADC2` | 压力采集 | `ADC_CHANNEL_15`（PB15），单端，采样 `ADC_SAMPLETIME_2CYCLES_5` |
| `TIM2` | 主 PWM | CH2 → PA1，输出 5%~95% 占空比 |
| `TIM3` | 任务调度 | 内部时钟 1 ms 周期中断 |
| `TIM16` | 流量捕获 | CH1 → PB4，**上升沿输入捕获** |

> 还有一路 `TIM17_CH1 → PA7` 的 50 Hz 舵机 PWM 在 `pwm.c` 里直接用寄存器方式启，
> 这条路在 `17403250.ioc` 暂未独立列 IP（共用 TIM 时钟源），若改 CubeMX 请补 IP。

### 9.3 NVIC 中断向量

| 中断 | 用途 | 抢占优先级 |
| --- | --- | --- |
| `SysTick_IRQn` | HAL 时基（1 ms tick） | 15（最低） |
| `TIM3_IRQn` | 任务调度分派 | 默认 |
| `TIM2_IRQn` | TIM2 全局中断（保留） | 默认 |
| `TIM1_UP_TIM16_IRQn` | TIM16 输入捕获 | 默认 |
| `HardFault` / `BusFault` / 等 | 默认 fault | 0 |

> HAL 调度在 `HAL_TIM_PeriodElapsedCallback` 里分发：`htim->Instance == TIM3 → timer_task_dispatch()`；`htim->Instance == TIM16 → flow_ic_capture()`（见 `main.c`）。

### 9.4 时钟树速查

```
HSE ──▶ /3 ──▶ ×20 ──▶ /2 ──▶ SYSCLK 80 MHz
                              ├─▶ HCLK  = 80 MHz
                              ├─▶ APB1 = 80 MHz（TIM2/3/16/17 时钟 = 80 MHz）
                              └─▶ APB2 = 80 MHz
```

- TIM2/3/16/17 全部挂在 APB1 定时器时钟，等于 SYSCLK = 80 MHz
- `flow.c` 里把 TIM16 预分频到 80，得到 1 MHz 计数时钟（`fre1 = 1 000 000 / Δcount`）

### 9.5 CubeMX 使用流程

1. **打开工程**：双击 `17403250.ioc` → STM32CubeMX 自动启动
2. **改外设**：在 Pinout & Configuration 里增删外设
3. **生成代码**：Project → Generate Code
   - 注意：CubeMX 只会重写带 `/* USER CODE BEGIN */` 标记之间的区域，**用户代码请全部放在这些块内**（已遵循）
4. **切 IDE**：Toolchain 可在 Project → Settings 里换成 `STM32CubeIDE` / `MDK-ARM` / `Makefile`

### 9.6 不要手改的目录

| 目录 | 谁生成 | 备注 |
| --- | --- | --- |
| `Drivers/STM32G4xx_HAL_Driver/Src/*.c` | HAL 包 | 升级 HAL 时整目录覆盖 |
| `Drivers/STM32G4xx_HAL_Driver/Inc/*.h` | HAL 包 | 同上 |
| `Drivers/CMSIS/` | CMSIS 包 | 同上 |
| `Core/Src/gpio.c` `tim.c` `adc.c` `stm32g4xx_it.c` `stm32g4xx_hal_msp.c` | **CubeMX** | 改 IO/中断请回 CubeMX |
| `Core/Inc/stm32g4xx_hal_conf.h` | CubeMX | 哪个 HAL 模块用 `#define HAL_xxx_MODULE_ENABLED` 开 |

### 9.7 应用代码（手写）vs 生成代码（CubeMX）划分

```
Core/Src/
├── main.c                    ✎ 手写：app_init()、回调分发、SystemClock_Config
├── app.c                     ✎ 手写：模块装载
├── settings.c                ✎ 手写：全局变量/阈值定义
├── key.c                     ✎ 手写
├── lcd_ui.c                  ✎ 手写
├── led.c                     ✎ 手写
├── pwm.c                     ✎ 手写
├── flow.c                    ✎ 手写
├── adc_sensor.c              ✎ 手写
├── timer_task.c              ✎ 手写
├── stm32g4xx_it.c            ⚙ CubeMX：中断向量入口（内有 USER CODE 区域）
├── stm32g4xx_hal_msp.c       ⚙ CubeMX：MSP 回调（时钟使能、GPIO 复用）
├── adc.c                     ⚙ CubeMX：MX_ADC2_Init
├── tim.c                     ⚙ CubeMX：MX_TIM2/3/16/17_Init
├── gpio.c                    ⚙ CubeMX：MX_GPIO_Init
└── system_stm32g4xx.c        ⚙ CMSIS：SystemInit / SystemCoreClock
```

> 面试时被问到"哪些是你写的、哪些是生成的"可以直接对照这张表。

### 9.8 常见 HAL 用法速查

| 需求 | 调用的 HAL API |
| --- | --- |
| ADC 单通道轮询读 | `HAL_ADC_Start` → `HAL_ADC_PollForConversion` → `HAL_ADC_GetValue` |
| ADC 内部偏移校准 | `HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED)` |
| PWM 启动 | `HAL_TIM_PWM_Start(&htimX, TIM_CHANNEL_Y)` |
| PWM 改占空比 | `__HAL_TIM_SetCompare(&htimX, TIM_CHANNEL_Y, ccr)` |
| 定时器中断 | `HAL_TIM_Base_Start_IT(&htimX)` |
| 输入捕获 | `HAL_TIM_IC_Start_IT(&htimX, TIM_CHANNEL_Y)` + `HAL_TIM_IC_CaptureCallback` |
| 获取系统 tick | `HAL_GetTick()`（1 ms 基准，依赖 SysTick） |

---

## 10. 编译与烧录

1. 用 **STM32CubeIDE** 或 **Keil MDK** 打开 `17403250.ioc`
2. 第一次打开用 CubeMX 生成代码
3. 编译烧录到开发板

> 项目里只保留 Keil 的 `.gitignore` 习惯（`MDK-ARM/Objects` 等被忽略），
> 如果改用 CubeIDE，请把 `Debug/` 等目录加进 `.gitignore`。

---

## 11. 关键设计点（面试问答预热）

> 这一节是给"以后的我"和面试官留的备忘，回答时直接对照源码。

1. **为什么用 1 ms 定时器做任务调度，而不是 SysTick 或裸循环？**
   - SysTick 被 HAL 占用；用单独 TIM3 1 ms 中断做"软件分时复用"，便于扩展更多周期任务，且不会因为长任务阻塞其他外设中断。

2. **为什么 PWM 斜坡要 1%/s？**
   - 比例阀/舵机突然跳变会引发水锤或机械冲击；斜坡可以软启动并避免过冲。

3. **零点校准为什么是长按 B3，而不是短按？**
   - 短按 B3 在 OUTP 页是"加"，与"校准"动作歧义；用长按 2 s 区分，同时长按不容易误触。

4. **LD2/LD3 为什么用 2 s 持续条件？**
   - 管路压力/流量瞬时波动较大；"持续 N 秒"是工业控制常见的去抖做法。

5. **74HC595 为什么要"全灭 → 锁存 → 拉低 → 锁存"两次？**
   - 第一次锁存保证寄存器在写入新值前是确定的全 1 状态，避免瞬间出现毛刺。

6. **F = fre1 / 200 系数怎么来的？**
   - 取决于流量传感器规格，本项目假设每升 200 脉冲，使用时按实际传感器 datasheet 调整（位于 `flow.c`）。

7. **流量累计为什么用 float `Q_acc` 缓存，而不是直接 +1？**
   - 避免小流量长时间累加丢失；满 1 L 才让 `Q_value` 整数 +1。

8. **MAIN 页为什么只刷 Line3~Line8？**
   - 标题/空行（Line1/Line2）不变；只刷数据行减少 LCD 写入次数，避免视觉闪烁。

9. **自动模式为什么用 0.5 Bar 死区？**
   - 压力传感器分辨率与噪声综合考虑；过窄会频繁动作，过宽控制精度下降。

10. **异常频段 `fre1 > 8000` 为什么钳到 8001？**
    - 既保证 LD1 能被点亮，又不会让下游用 `fre1` 做除法时出现除零或溢出。

---

## 11. 后续可优化方向

- [ ] 阈值 `FH/FL/PL/DH` 改用 Flash / EEPROM 持久化
- [ ] 累计流量 `Q_value` 掉电保存
- [ ] 加 RS485 / Modbus 上位机通信
- [ ] 把 "MAN/AUTO 切换" 做成无扰切换（沿用当前 D_actual）
- [ ] ADC 改用 DMA + 定时器触发，减轻 CPU
- [ ] 流量计系数改为运行时可标定
- [ ] 报警增加蜂鸣器 + 故障码 LCD 显示

---

## 13. 调试小抄

| 现象 | 排查点 |
| --- | --- |
| LCD 全黑 | 背光？`lcd_ui_init` 是否调用 |
| 流量一直 0 | 检查 TIM16 CH1 IO；传感器是否有脉冲；`fre1` 是否被钳到 0 |
| 压力一直 0 | 检查 `Voffset`，按 B3 长按校准一次 |
| LED 不亮 | 74HC595 锁存脚 PD2 是否有波形；PC8~PC11 输出电平 |
| PWM 没输出 | 检查 `MX_TIM2_Init` 是否开了 CH2；`HAL_TIM_PWM_Start` 是否调用 |
| 切页不动 | 按键是否被消抖吞掉；`Lcd_Refresh_Flag` 是否置 1 |

---

**版本**: v1.0
**最后更新**: 与仓库当前代码一致
**作者**: 仅供面试 + 自我复习用
