# YOLO模型导出和优化工具使用指南

本工具专为树莓派等边缘设备优化，支持多种模型格式导出和量化。

## 📦 快速开始

### 1. 导出ONNX模型（推荐）

```bash
# 基础ONNX导出
python tools/export_model.py --model models/best.pt --format onnx

# ONNX + INT8量化（最快，轻微精度损失）
python tools/export_model.py --model models/best.pt --format onnx --int8

# ONNX + FP16半精度（较快，无精度损失）
python tools/export_model.py --model models/best.pt --format onnx --half
```

### 2. 导出NCNN模型（树莓派最优）

```bash
# NCNN + INT8量化
python tools/export_model.py --model models/best.pt --format ncnn --int8

# NCNN + FP16
python tools/export_model.py --model models/best.pt --format ncnn --half
```

### 3. 导出TFLite模型

```bash
# TFLite + INT8量化
python tools/export_model.py --model models/best.pt --format tflite --int8
```

### 4. 批量导出所有推荐格式

```bash
python tools/export_model.py --model models/best.pt --batch
```

## 🔧 参数说明

| 参数 | 简写 | 说明 | 默认值 |
|------|------|------|--------|
| `--model` | `-m` | 输入模型路径 | `models/best.pt` |
| `--format` | `-f` | 导出格式（onnx/ncnn/tflite等） | `onnx` |
| `--int8` | - | 启用INT8量化 | 关闭 |
| `--half` | - | 启用FP16半精度 | 关闭 |
| `--imgsz` | - | 输入图像尺寸（320/416/512/640） | `640` |
| `--output-dir` | `-o` | 输出目录 | `./exported_models` |
| `--batch` | - | 批量导出所有推荐格式 | 关闭 |
| `--dynamic` | - | 使用动态输入尺寸 | 关闭 |

## 🚀 树莓派部署指南

### 方案一：ONNX + ONNXRuntime（推荐，最简单）

**1. 导出模型**
```bash
python tools/export_model.py -m models/best.pt -f onnx --int8
```

**2. 树莓派安装依赖**
```bash
pip install onnxruntime
# 或轻量版
pip install onnxruntime-raspberry
```

**3. 修改配置文件**
编辑 `common/config.py`：
```python
MODEL_PATH = "../exported_models/best_int8.onnx"
```

**4. 运行系统**
```bash
python main.py
```

### 方案二：NCNN（性能最优）

**1. 导出模型**
```bash
python tools/export_model.py -m models/best.pt -f ncnn --int8
```

**2. 树莓派编译NCNN**
```bash
git clone https://github.com/Tencent/ncnn.git
cd ncnn
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DNCNN_OPENMP=OFF ..
make -j4
sudo make install
```

**3. 使用C++推理**（需要额外开发）

### 方案三：TFLite（平衡方案）

**1. 导出模型**
```bash
python tools/export_model.py -m models/best.pt -f tflite --int8
```

**2. 树莓派安装依赖**
```bash
pip install tflite-runtime
```

## ⚡ 性能对比

| 格式 | 量化 | 树莓派4B FPS | 精度损失 | 推荐场景 |
|------|------|--------------|----------|----------|
| PyTorch (.pt) | 无 | ~2-3 | 无 | 开发测试 |
| ONNX | 无 | ~5-8 | 无 | 通用场景 |
| ONNX | FP16 | ~8-12 | 无 | 需要精度 |
| ONNX | INT8 | ~12-20 | 小(~2%) | **推荐** |
| NCNN | INT8 | ~15-25 | 小(~2%) | 极致性能 |
| TFLite | INT8 | ~10-18 | 小(~2%) | 平衡方案 |

> 注：FPS数据基于YOLOv8n模型，实际性能取决于模型大小和树莓派型号

## 📝 使用优化后的模型

系统已自动支持加载优化后的模型，无需修改代码：

```python
# 直接在配置中指定优化后的模型路径
MODEL_PATH = "../exported_models/best_int8.onnx"

# 系统会自动识别格式并选择最优推理引擎
```

### 支持的模型格式

- ✅ `.pt` - PyTorch原始模型
- ✅ `.onnx` - ONNX格式（自动使用ONNXRuntime）
- ✅ `.xml`/`.bin` - OpenVINO格式
- ✅ `.ncnn` - NCNN格式
- ✅ `.tflite` - TFLite格式

## ⚠️ 注意事项

1. **INT8量化**可能需要校准数据集，首次导出时如果失败，尝试移除`--int8`参数
2. **图像尺寸**越小速度越快，320x320比640x640快约4倍
3. **树莓派型号**建议至少使用树莓派4B（4GB内存）
4. **散热**很重要，长时间运行建议加装散热片或风扇
5. **电源**要稳定，建议使用官方电源（5V 3A）

## 🔍 故障排除

### 问题1：INT8量化失败
```bash
# 解决方案：使用FP16替代
python tools/export_model.py -m models/best.pt -f onnx --half
```

### 问题2：ONNXRuntime未安装
```bash
pip install onnxruntime
```

### 问题3：模型加载后精度下降
- 尝试使用FP16而非INT8
- 检查校准数据集是否代表性强
- 使用更大的基础模型（如YOLOv8s替代YOLOv8n）

### 问题4：推理速度不理想
- 降低输入图像尺寸：`--imgsz 320`
- 启用跳帧检测：在`config.py`中设置`DETECTION_INTERVAL = 2`
- 关闭不必要的可视化功能

### 问题5：STM32 串口没有响应
先不要运行完整 YOLO 主程序，用最小链路测试：

```powershell
python tools/serial_link_test.py --serial-port COM6
```

该工具默认使用 `9600 8N1`，打开串口后会等待启动串；如需验证 `STM32 STEP READY`，按提示复位 STM32。

正常应看到：

```text
TX: PING
RX: PONG
TX: POS
RX: OK 0 0
TX: MOVE 10 0
RX: OK 10 0
```

如果 `PING` 都超时，优先检查串口助手或工具是否发送了换行符、PA9/PA10 是否交叉接线、GND 是否共地、COM 口是否被其他程序占用，以及 STM32 复位后是否输出 `STM32 STEP READY`。

如果发送 `PING` 后收到的是 `PING` 或类似 `?ING`，说明看到的是回显，不是本项目固件的 `PONG` 响应。确认 Keil 已经 Download 当前 `stm32-src/Objects/main.hex`，不要烧录旧的 `test_pro.hex`；同时关闭串口助手本地回显，并排除 USB-TTL TX/RX 短接。

## 📚 进阶优化

### 1. 模型剪枝
```python
from ultralytics import YOLO

model = YOLO('models/best.pt')
model.prune(pruning_rate=0.3)  # 剪枝30%
model.export(format='onnx', int8=True)
```

### 2. 知识蒸馏
使用大模型训练小模型，保持精度的同时提升速度

### 3. TensorRT优化（适用于Jetson系列）
```bash
python tools/export_model.py -m models/best.pt -f engine --half
```

## 📞 技术支持

如有问题，请查看：
- Ultralytics官方文档：https://docs.ultralytics.com/
- ONNXRuntime文档：https://onnxruntime.ai/
- NCNN项目：https://github.com/Tencent/ncnn
