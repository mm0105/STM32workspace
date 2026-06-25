#!/usr/bin/env bash

# YOLO云台人耳跟踪系统启动脚本
# 自动激活虚拟环境并运行主程序

# 兼容误用 `sh ./start.sh` 的情况：自动切换到 bash 执行
if [ -z "${BASH_VERSION:-}" ]; then
    exec bash "$0" "$@"
fi

set -e

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# 虚拟环境路径（优先 .venv，其次 venv）
if [ -d ".venv" ]; then
    VENV_DIR=".venv"
elif [ -d "venv" ]; then
    VENV_DIR="venv"
else
    echo "❌ 未找到虚拟环境目录（.venv 或 venv）"
    echo "请先创建虚拟环境并安装依赖："
    echo "  python3 -m venv .venv"
    echo "  source .venv/bin/activate"
    echo "  python -m pip install -r requirements.txt"
    exit 1
fi

# 激活虚拟环境
echo "激活虚拟环境: $VENV_DIR"
# shellcheck disable=SC1090
. "$VENV_DIR/bin/activate"

# 检查 Python 可用性
if command -v python >/dev/null 2>&1; then
    PYTHON_BIN="python"
elif command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="python3"
else
    echo "❌ Python 未找到"
    exit 1
fi

# 显示 Python 版本
echo "Python版本: $($PYTHON_BIN --version)"
echo ""

# 依赖自检（完整检查所有库）
# 尝试一次性导入所有关键库，若任意导入失败则重新安装
if "$PYTHON_BIN" -c "
import sys
mods = [
    'torch',
    'torchvision',
    'cv2',          # opencv‑python
    'ultralytics',
    'numpy',
    'scipy',
    'onnx',
    'onnxruntime',
    'serial',       # pyserial
    'pygame',
]
missing = []
for m in mods:
    try:
        __import__(m)
    except Exception:
        missing.append(m)
if missing:
    print('缺失依赖:', ', '.join(missing))
    print('当前 Python:', sys.executable)
    sys.exit(1)
"; then
    echo "所有依赖已就绪"
else
    echo "检测到缺失的依赖，正在重新安装 requirements.txt ..."
    "$PYTHON_BIN" -m pip install --upgrade pip
    "$PYTHON_BIN" -m pip install -r requirements.txt
fi

# 运行主程序
"$PYTHON_BIN" main.py "$@"

# 退出时停用虚拟环境（若可用）
if command -v deactivate >/dev/null 2>&1; then
    deactivate
fi
