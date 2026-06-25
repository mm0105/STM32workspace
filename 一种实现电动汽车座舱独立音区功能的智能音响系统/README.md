# YOLO 云台人耳跟踪系统

> 基于 YOLOv8 人耳检测 + 步进电机云台的视觉跟随系统：USB 摄像头采集 → 上位机 YOLO 推理 + IOU 多目标跟踪 → UART 协议 → STM32F103 下位机 → 双轴步进电机 → 视觉随动。

> 仓库展示标签：YOLO / STM32F103 / 步进电机云台

本项目是大创（大学生创新创业训练计划）课题“基于 YOLO 与步进电机的视觉跟随云台”的实现代码，覆盖了 **数据集 → 模型训练 → 端侧部署 → 上位机控制 → 下位机固件** 的完整链路。仓库主分支为整合后的 `yolo-use-stm32/`，外层另有 `yolo-use/` 纯上位机分支（不含 STM32）作为历史参考。

---

## 目录

- [演示](#演示)
- [主要特性](#主要特性)
- [系统架构](#系统架构)
- [仓库目录](#仓库目录)
- [硬件准备](#硬件准备)
- [快速开始](#快速开始)
- [串口协议](#串口协议)
- [STM32 固件说明](#stm32-固件说明)
- [上位机配置](#上位机配置)
- [操作快捷键](#操作快捷键)
- [常见问题](#常见问题)
- [后续规划](#后续规划)
- [致谢](#致谢)

---

## 演示

> 仓库内未上传演示视频/截图，可使用以下方式自行复现：
>
> 1. 将训练好的 YOLO 模型放到 `yolo-src/models/`
> 2. 给 STM32 烧录固件（见 [快速开始](#快速开始)）
> 3. 连接 USB 摄像头与步进电机云台
> 4. 运行上位机，对着摄像头展示人耳即可看到云台跟随

---

## 主要特性

- **单类别耳检测**：YOLOv8 训练 ear 单类别模型（也支持同时输出 face）
- **多目标跟踪**：基于 IOU 的匈牙利算法关联 + Track ID，EMA 中心点平滑
- **目标选择策略**：confidence / proximity / hybrid 三种可配评分
- **视觉闭环**：检测框中心相对画面中心的像素误差 → 步进电机步数映射
- **稳定控制**：中心死区 + 指令冷却 + 微分阻尼，避免云台在画面中心反复抖动
- **单飞执行（single-flight）**：移动未完成时拒绝新指令，下位机 `busy` 状态机 + 上位机 `_in_flight` 标志双重保护
- **跨平台运行**：Windows（PowerShell）/ Linux / 树莓派，一键启动脚本
- **协议可控**：ASCII 文本协议 `MOVE pan tilt\n` / `PING\n` → `OK` / `BUSY` / `ERR` / `PONG`，串口助手可抓包调试
- **下位机独立**：STM32F103 + 标准外设库实现，USART 接收中断 + 行缓冲解析、定时器中断产生步进脉冲、双轴同步、方向-脉冲时序配合
- **可选 GPIO 直驱**（旧版）：树莓派本地 GPIO 直接驱动 STEP/DIR，作为串口方案的备选

---

## 系统架构

```
┌─────────────────── 上位机（Python） ───────────────────┐
│                                                          │
│  main.py                                                 │
│   └─ YOLOTrackingSystem（tracking_system.py）            │
│       ├─ cv2.VideoCapture ── 摄像头帧                   │
│       ├─ ultralytics / onnxruntime ── YOLO 推理         │
│       ├─ EarTracker（IOU 关联 + Track ID + EMA）        │
│       ├─ 像素误差 → 步数映射（config.py）               │
│       ├─ SerialStepperGimbal（serial_stepper_gimbal.py）│
│       │    ├─ single-flight（_in_flight 标志）          │
│       │    ├─ 发送 "MOVE <pan> <tilt>\n"               │
│       │    └─ 心跳 "PING\n" → "PONG"                    │
│       └─ VisualizationManager（visualization.py）       │
└──────────────────────────┬───────────────────────────────┘
                           │ UART 9600 8N1（3.3V TTL）
                           │ ASCII 协议
                           ▼
┌─────────────────── 下位机（STM32 C） ───────────────────┐
│                                                          │
│  main.c                                                 │
│   ├─ USART1_Init(9600) — PA9/PA10 复用推挽 + 上拉输入  │
│   ├─ Stepper_Init() — PA0/PA1/PB0/PB2 推挽输出 + TIM2  │
│   └─ while(1) USART1_ProcessCommand()                  │
│        ├─ PING → PONG                                   │
│        ├─ MOVE pan tilt → Stepper_StartMove            │
│        └─ 完成 → OK                                      │
│                                                          │
│  USART1_IRQHandler  ─► 字符攒行                         │
│  Stepper_TIM2_IRQHandler ─► 翻转 STEP 脉冲              │
└──────────────────────────┬───────────────────────────────┘
                           │ PA0 / PB0 / PA1 / PB2
                           ▼
                   ┌────────────────────┐
                   │  步进驱动器 A / B  │
                   │      ↓             │
                   │  Pan 步进 / Tilt    │
                   └────────────────────┘
```

---

## 仓库目录

```
yolo-use-stm32/                          ← 整合后主分支（推荐）
└── yolo-use-stm32/
    ├── yolo-src/                        ← 上位机（Python）
    │   ├── main.py                      唯一主入口
    │   ├── bestuse.py                   纯检测最小参考脚本
    │   ├── common/
    │   │   ├── config.py                全局配置（参数统一在这里改）
    │   │   ├── serial_stepper_gimbal.py 串口步进控制器（主线）
    │   │   ├── rpi_gpio_stepper_gimbal.py 树莓派 GPIO 直驱（备选）
    │   │   └── visualization.py         检测框 + 中心十字
    │   ├── yolo_version/
    │   │   ├── ear_tracker.py           IOU 跟踪 + 评分
    │   │   └── tracking_system.py       主系统
    │   ├── models/                      best.pt / best.onnx / best_int8.onnx
    │   ├── tools/
    │   │   ├── export_model.py          PT → ONNX/NCNN/TFLite 导出
    │   │   └── README.md                导出工具使用说明
    │   ├── requirements.txt
    │   ├── start.sh / start.ps1
    │   └── README.md                    精简版上位机说明
    │
    ├── stm32-src/                       ← 下位机（Keil 工程）
    │   ├── main.uvprojx                 Keil 工程文件
    │   ├── user/                        业务源码
    │   │   ├── main.c                   主循环
    │   │   ├── usart.c / usart.h        USART1 协议
    │   │   ├── stepper.c / stepper.h    步进脉冲
    │   │   ├── stm32f10x_it.c / .h      中断分发
    │   │   ├── exti.c / key.c / led.c   辅助示例
    │   │   └── stm32f10x_conf.h         库配置
    │   ├── library/                     标准外设库 V3.5
    │   ├── public/                      SysTick 延时
    │   ├── start/                       启动文件 + CMSIS
    │   ├── Objects/main.hex             编译产物（可直接烧录）
    │   └── 工程配置说明.md              Keil 配置参考
    │
    └── README.md                        ← 当前文档
```

> 仓库外层另有 `yolo-use/` 纯上位机历史分支（无 STM32 内容），可作为上位机的最小参考实现。

---

## 硬件准备

| 部件 | 型号 / 规格 | 数量 | 备注 |
|---|---|---|---|
| MCU | STM32F103C8T6（最小系统板） | 1 | 主控，72MHz |
| 上位机 | PC / 树莓派（任意能跑 Python 3 的设备） | 1 | YOLO 推理 |
| USB 摄像头 | 任意 UVC 摄像头 | 1 | 推荐 MJPG 640×480@30fps |
| 步进电机 | 1.8° 整步角 + 32 细分 | 2 | Pan / Tilt 各一 |
| 步进驱动器 | A4988 / DRV8825 等 | 2 | STEP/DIR 输入 |
| 串口 | USB-TTL（CH340 / CP2102） | 1 | 3.3V TTL，**勿接 5V** |
| 电源 | 12V 电源（依电机电压） | 1 | 给驱动器供电 |
| 机械结构 | 双轴云台（Pan + Tilt） | 1 | 自制或成品 |

### 接线

```
上位机(PC/树莓派)                STM32F103C8                步进驱动器
─────────────────              ────────────              ────────────
USB 摄像头 <─── 摄像头帧 ───>   USART1 TX(PA9)  ─── RX ──> (上位机方向)
                               USART1 RX(PA10) <── TX ──  (无需回环, 见下)
                               (此处实际是上位机 USB-TTL 接到 PA9/PA10)

USB-TTL (3.3V)                 STM32F103C8
─────────────                  ────────────
   TX ─────────────────────►   PA10 (USART1 RX)
   RX ◄─────────────────────   PA9  (USART1 TX)
   GND ─────────────────────►  GND

                               STM32F103C8                步进驱动器
                               ────────────              ────────────
                               PA0  ─────────────────►   Pan STEP
                               PB0  ─────────────────►   Pan DIR
                               PA1  ─────────────────►   Tilt STEP
                               PB2  ─────────────────►   Tilt DIR
                               +3.3V / GND ───────────►  驱动器 VCC / GND
                                                        （驱动器单独 12V 供电）
```

> 实际项目里：**摄像头接到上位机（PC/树莓派）**，**USB-TTL 串口接 STM32**。上位机与 STM32 不直接相连，靠两段 USB 链路 + 串口线互连（笔记本 ↔ USB-TTL ↔ STM32）。

---

## 快速开始

### 0. 准备模型（首次使用）

把训练好的 YOLO 模型放到 `yolo-src/models/`，优先级为：
```
best.pt > best_int8.onnx > best.onnx
```

模型导出（可选）：
```bash
# ONNX + INT8 量化（树莓派 / CPU 推理推荐）
python yolo-src/tools/export_model.py \
    --model yolo-src/models/best.pt \
    --format onnx --int8
```

详见 [`yolo-src/tools/README.md`](yolo-src/tools/README.md)。

### 1. 烧录 STM32 固件

#### Windows / Keil
1. 打开 [`stm32-src/main.uvprojx`](stm32-src/main.uvprojx)
2. 确认芯片选型：STM32F103C8 / Medium-density
3. `Build`（编译）→ 产物在 `stm32-src/Objects/main.axf`
4. 配置下载器（ST-Link / J-Link / 串口 ISP），`Load` 烧录 `main.hex`

#### 串口 ISP（无仿真器时）
- BOOT0 接高，BOOT1 接低，复位后通过 USART1 用 FlyMCU / mcuisp 烧录 `main.hex`
- 烧完把 BOOT0 拉低，复位运行

### 2. 安装上位机依赖

#### Windows（PowerShell）
```powershell
cd yolo-src
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

#### Linux / 树莓派
```bash
cd yolo-src
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
```

### 3. 连接硬件
- USB 摄像头插入上位机
- USB-TTL 接到上位机与 STM32，确认 COM 口 / `/dev/ttyUSB*` / `/dev/ttyAMA0`
- 给 STM32 + 驱动器上电

### 4. 运行

#### Windows
```powershell
.\start.ps1 --serial-port COM6
```

#### Linux / 树莓派
```bash
./start.sh --serial-port /dev/serial0
```

也可以直接：
```bash
python main.py --serial-port COM6
```

#### 命令行参数
```
--serial-port    指定 STM32 串口（Windows: COMx；Linux: /dev/ttyUSB0 等）
--baudrate       串口波特率，默认 9600
```

不指定 `--serial-port` 时程序会自动探测候选端口。

### 5. 启动流程观察
启动后日志应当出现：
```
[初始化] 云台串口启动成功: COM6, 同步初始零点...
  [Pan] 启动角: 0.0° → 0 steps
  [Tilt] 开机工作角: -18.0° → -320 steps
  ✓ 云台开机工作位置命令已发送

启动摄像头...
摄像头启动成功: 640x480
按 q 退出，空格暂停
```

云台会先抬头到工作角（`-18°`），然后开始跟踪。

---

## 串口协议

### 物理层
- 波特率 `9600`，数据位 `8`，校验 `None`，停止位 `1`
- TTL 3.3V

### 协议表

| 方向 | 命令 / 响应 | 含义 |
|---|---|---|
| 上→下 | `MOVE <pan_steps> <tilt_steps>\n` | 相对移动步数（正负表示方向） |
| 上→下 | `PING\n` | 链路心跳 |
| 下→上 | `OK\r\n` | `MOVE` 执行完成 |
| 下→上 | `PONG\r\n` | `PING` 响应 |
| 下→上 | `BUSY\r\n` | 上一条 `MOVE` 还在执行 |
| 下→上 | `ERR\r\n` | 解析失败 |
| 下→上 | `STM32 STEP READY\r\n` | 上电握手（启动时一次） |

### 命令样例（用串口助手调试）
```
发送: PING
收到: PONG

发送: MOVE 10 0
收到: OK

发送: MOVE 0 -20
收到: OK

发送: MOVE abc 5
收到: ERR
```

### `MOVE` 步数含义
- 1 step = 1.8° / 32 细分 = **0.05625°/步**
- `pan_steps > 0` 顺时针 / `< 0` 逆时针（依 DIR 引脚电平）
- `tilt_steps > 0` 抬头 / `< 0` 低头
- 单条命令的步数上限由 STM32 固件保护，建议上位机 clamp 在 `MAX_*_STEPS_PER_CMD`

---

## STM32 固件说明

### 工程结构
```
stm32-src/
├── main.uvprojx          Keil MDK 工程
├── user/                 业务源码
├── library/              STM32F10x 标准外设库 V3.5
├── public/               SysTick 延时
├── start/                启动文件 + CMSIS
├── Objects/main.hex      编译产物
└── README.md             详细固件说明
```

### 关键模块
| 文件 | 职责 |
|---|---|
| [`main.c`](stm32-src/user/main.c) | 主循环：`USART1_Init` + `Stepper_Init` + `while(1) USART1_ProcessCommand` |
| [`usart.c`](stm32-src/user/usart.c) | USART1 初始化、字符发送、**RXNE 中断攒行**、`MOVE`/`PING` 解析、回 `OK`/`PONG`/`ERR` |
| [`stepper.c`](stm32-src/user/stepper.c) | GPIO 初始化、**TIM2 中断调度 STEP/DIR 脉冲**、双轴同步（短轴提前停）、`busy` 状态机 |
| [`stm32f10x_it.c`](stm32-src/user/stm32f10x_it.c) | 异常向量表：转发 USART1/TIM2 IRQHandler 到业务层 |

### 关键设计

- **USART1 RXNE + 行缓冲**：在中断里只攒字符，遇到 `\r` 或 `\n` 才置 `g_line_ready = 1`；主循环解析，规避在 ISR 里 `strtol`。
- **方向先于脉冲**：`Stepper_StartMove` 先锁 DIR，再由 TIM2 中断翻 STEP，符合步进驱动器典型时序。
- **临界区互斥**：`Stepper_StartMove` 用 `__disable_irq/__enable_irq` 包住"锁 DIR + 改 busy + 开 TIM2"三步，避免与 TIM2 ISR 冲突。
- **TIM2 500μs 周期**：`Prescaler=72-1`（72MHz/72=1MHz）、`Period=500-1`（1MHz/500=2kHz），等价 1ms/步（1kHz 脉冲率，可调）。
- **双轴同步**：`max_steps = max(|pan|, |tilt|)` 作为总步数；每步判断 `i < |pan|` 才翻 Pan STEP，短轴先到顶。
- **NVIC 优先级**：USART1 = 3/3（低），TIM2 = 2/0（高），保证脉冲不被串口打断。
- **状态机反馈**：主循环轮询 `Stepper_IsBusy()`，一旦为 0 立刻回 `OK`，上位机 single-flight 才放行下一条。

### 时钟
- HSE = 8MHz × 9 = SYSCLK = 72MHz
- AHB = 72MHz, APB1 = 36MHz, APB2 = 72MHz
- 见 [`stm32-src/start/system_stm32f10x.c`](stm32-src/start/system_stm32f10x.c)

### 引脚规划

| 功能 | 引脚 | 备注 |
|---|---|---|
| USART1 TX | PA9 | 复用推挽 |
| USART1 RX | PA10 | 浮空输入 |
| Pan STEP | PA0 | 推挽输出 |
| Pan DIR | PB0 | 推挽输出 |
| Tilt STEP | PA1 | 推挽输出 |
| Tilt DIR | PB2 | 推挽输出 |

详见 [`stm32-src/README.md`](stm32-src/README.md) 与 [`stm32-src/工程配置说明.md`](stm32-src/工程配置说明.md)。

---

## 上位机配置

主要参数在 [`yolo-src/common/config.py`](yolo-src/common/config.py)：

| 配置项 | 默认 | 含义 |
|---|---|---|
| `MODEL_PATH` | `models/best.pt` | 模型路径（自动回落到 `best_int8.onnx`） |
| `DETECTION_INTERVAL` | `2` | 跳帧检测（1 = 每帧都检测） |
| `EAR_CLASS_ID` | `1` | 数据集中 ear 的类别 id |
| `MAX_LOST_FRAMES` | `10` | 跟踪丢失多少帧后放弃 |
| `IOU_MATCH_THRESHOLD` | `0.3` | IOU 关联阈值 |
| `TARGET_SELECTION_MODE` | `hybrid` | `confidence` / `proximity` / `hybrid` |
| `WEIGHT_CONFIDENCE` / `WEIGHT_PROXIMITY` | `0.5` / `0.5` | 混合评分权重 |
| `EMA_SMOOTH_FACTOR` | `0.7` | 中心点 EMA 平滑系数 |
| `DIRECTION_DEAD_ZONE` | `15` | 死区（像素），误差小于此值不动 |
| `STEP_MOVE_COOLDOWN_SECONDS` | `0.05` | 指令冷却（秒），~20Hz 修正 |
| `TRACKING_DAMPING_FACTOR` | `0.3` | 微分阻尼（0~1）防超调 |
| `RPI_STEPPER_PIXELS_PER_STEP_X/Y` | `18.0` | 像素误差 → 步数映射（每 N 像素 = 1 步） |
| `RPI_STEPPER_MAX_PAN/TILT_STEPS_PER_CMD` | `120` | 单命令步数限幅 |
| `RPI_STEPPER_PULSE_US` | `900` | 脉冲间隔（us）（仅 GPIO 直驱生效） |
| `RPI_STEPPER_SETTLE_MARGIN_S` | `0.03` | 机械稳定裕量（s） |
| `RPI_STEPPER_SERIAL_PORT` | `None` | 串口，`None` 表示自动探测 |
| `RPI_STEPPER_SERIAL_BAUDRATE` | `9600` | 串口波特率 |
| `STEPPER_STEP_ANGLE` / `STEPPER_MICROSTEP` | `1.8°` / `32` | 步进电机参数 |
| `PAN_RANGE` / `TILT_RANGE` | `(-20, 20)` / `(-28, 0)` | 机械限位（度） |
| `TILT_BOOT_TARGET_ANGLE` | `-18.0°` | 开机后云台自动抬头到的工作角 |
| `TARGET_LOST_RETURN_HOME_SECONDS` | `5.0` | 目标丢失多久后归位（s） |
| `ENABLE_SPEAKER` / `SPEAKER_*` | `True` | 喇叭循环音乐（pygame / ALSA） |
| `ENABLE_DISPLAY` | `True` | 是否弹出 OpenCV 窗口 |

> 修改后**必须重启**上位机生效。

---

## 操作快捷键

| 按键 | 动作 |
|---|---|
| 空格 | 暂停 / 继续（仅在 `ENABLE_DISPLAY=True` 时可用） |
| `q` | 退出 |

---

## 常见问题

### Q1: 上位机找不到串口
- Windows：设备管理器查看 COM 号，确保没有别的程序占用（XCOM、串口助手等）
- Linux：`ls -l /dev/ttyUSB* /dev/ttyAMA*`，确认用户有 `dialout` 权限：`sudo usermod -aG dialout $USER` 然后重新登录
- 用 `--serial-port` 手动指定

### Q2: 启动后云台不动
- 检查 STM32 串口线（TX/RX 不能接反，GND 必接）
- 用 USB-TTL 直接接 STM32 串口助手发 `PING`，应回 `PONG`
- 检查限位：`PAN_RANGE` / `TILT_RANGE` 是否被 `TILT_BOOT_TARGET_ANGLE` 包含

### Q3: 云台来回抖动
- 调大 `DIRECTION_DEAD_ZONE`（如 25~30）
- 调大 `STEP_MOVE_COOLDOWN_SECONDS`（如 0.1）
- 调大 `TRACKING_DAMPING_FACTOR`（如 0.5）

### Q4: 步进电机丢步
- 检查电源：驱动器 12V 电流是否够
- 减小 `RPI_STEPPER_MAX_*_STEPS_PER_CMD`（步数太多一次脉冲率顶不上）
- 减小 `RPI_STEPPER_SINGLE_MOVE_DEG`（单次转角不要太大）
- 检查接线：STEP/DIR 是否接触良好

### Q5: YOLO 检测不到人耳
- `EAR_CLASS_ID` 是否正确（数据集 yaml 里 `names` 顺序决定 id）
- 模型路径是否正确，`ultralytics` 是否能加载（看启动日志）
- 摄像头翻转：`FLIP_VERTICAL` / `FLIP_HORIZONTAL` 是否与实际安装一致

### Q6: 树莓派没 GPIO 库
- `pip install RPi.GPIO`（仅树莓派系统镜像）
- 当前主线已切到串口方案，本项仅在 `RPI_STEPPER_USE_GPIO_DIRECT=True` 时需要

### Q7: 喇叭没声音
- Windows：检查默认播放设备
- Linux：`SPEAKER_AUDIO_DRIVER=alsa`，`SPEAKER_ALSA_DEVICE=plughw:2,0`（`aplay -l` 查看设备号）
- 文件 `models/mc.mp3` 是否存在

---

## 后续规划

- [ ] 增加 PID 控制替代纯比例 + 阻尼
- [ ] 增加限位开关硬件保护（目前只有软件 clamp）
- [ ] 抽象串口协议为结构体（当前为 ASCII 文本，未来可换二进制）
- [ ] STM32 端加看门狗 + 串口断连检测
- [ ] 上位机加 Web 界面（FastAPI + WebSocket）
- [ ] 模型换 YOLOv11 / RT-DETR，对比 mAP 与 FPS
- [ ] 增加数据采集标注工具链的整合脚本

---

## 致谢

- **YOLOv8 / ultralytics**：检测框架
- **OpenCV**：图像处理与可视化
- **pyserial**：跨平台串口
- **STM32 标准外设库 V3.5**：固件库
- 团队成员在数据集标注、模型训练、报告撰写上的协作

---

> 本项目为大创课题实现，欢迎 Star / Fork / Issue。  
> 团队成员：大创组成员（按姓氏首字母）  
> 课题方向：嵌入式视觉 / 边缘 AI
