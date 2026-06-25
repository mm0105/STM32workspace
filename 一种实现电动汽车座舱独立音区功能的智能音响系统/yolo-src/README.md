# YOLO 云台人耳跟踪系统（精简版）

## 项目概述

这是一个以“稳定可用”为目标的重构版本：
- 仅保留**耳朵检测 + 云台跟踪**主链路
- 删除人脸辅助预测、PID旧实现等冗余模块
- 保留多目标跟踪、平滑滤波、云台控制和基础可视化
- 云台控制已切换为**树莓派 -> UART -> 步进下位机**，并采用**single-flight异步执行**（移动未完成前拒绝新指令）

> 你的模型效果已经足够好，项目逻辑也应该“少而精”。

---

## 当前结构

```text
yolo-use/
├── common/
│   ├── __init__.py
│   ├── config.py                # 全局配置
│   ├── serial_stepper_gimbal.py # 串口步进控制（PC/树莓派 -> STM32，single-flight）
│   └── visualization.py         # 基础检测框可视化
├── yolo_version/
│   ├── __init__.py
│   ├── ear_tracker.py           # 多目标跟踪/ID管理
│   ├── tracking_system.py       # 精简主系统（核心）
│   └── start.py                 # 兼容入口（转发到 main.py）
├── models/
│   ├── best.pt
│   ├── best.onnx
│   └── best_int8.onnx
├── main.py                      # 唯一主入口
├── bestuse.py                   # 原始参考脚本（仅检测）
├── requirements.txt
└── README.md
```

---

## 运行

1. 创建虚拟环境后安装依赖。Windows 下建议始终用当前解释器调用 pip：

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
```

Linux / 树莓派：

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
```

2. Windows PowerShell 推荐运行：

```powershell
.\start.ps1 --serial-port COM6
```

3. Linux / 树莓派推荐运行：

```bash
./start.sh --serial-port /dev/serial0
```

也可以直接运行：

```bash
python main.py --serial-port COM6
```

程序会自动：
- 扫描可用摄像头
- 自动/手动选择摄像头
- 加载模型并启动耳朵跟踪

---

## 核心能力

- **耳朵检测**：YOLO 单类别链路（ear）
- **多目标跟踪**：IOU 关联 + Track ID
- **稳定控制**：dead zone + 连续稳定识别门限 + 指令冷却
- **云台动作**：像素误差映射为步进指令，经 `SerialStepperGimbal` 异步下发到 STM32
- **执行约束**：未完成当前移动前，不接受下一条移动命令
- **轻量可视化**：只显示耳朵检测框，降低干扰

---

## 快捷键

- `空格`：暂停/继续
- `q`：退出

---

## 配置建议（`common/config.py`）

常用项：
- `MODEL_PATH`：模型路径
- `DETECTION_INTERVAL`：跳帧检测（1为每帧）
- `DIRECTION_DEAD_ZONE`：中心死区，避免抖动
- `RPI_STEPPER_PIXELS_PER_STEP_X/Y`：像素到步数映射
- `RPI_STEPPER_MAX_PAN_STEPS_PER_CMD` / `RPI_STEPPER_MAX_TILT_STEPS_PER_CMD`：单命令步数限幅
- `RPI_STEPPER_PULSE_US`：脉冲间隔（us）
- `RPI_STEPPER_SETTLE_MARGIN_S`：机械稳定裕量（s）
- `RPI_STEPPER_SERIAL_PORT`、`RPI_STEPPER_SERIAL_BAUDRATE`：串口参数
- `SPEAKER_AUDIO_DRIVER`：音频驱动，`auto` 会在 Linux/树莓派使用 ALSA，在 Windows 使用 pygame 默认驱动

STM32 串口协议、接线和人工验收步骤见 `../docs/stm32_serial_stepper_interface.md`。

---

## 说明

`bestuse.py` 依然保留，作为“纯检测最小参考”；
当前主系统是在此思路上，补上了云台控制与跟踪稳定性。
