#!/usr/bin/env python3
"""
YOLO云台人耳跟踪系统 - 主入口
"""

import os
import argparse
import cv2

# 导入基础配置
from common.config import MODEL_PATH, ENABLE_DISPLAY, RPI_STEPPER_SERIAL_BAUDRATE


def check_available_cameras():
    """检查可用的摄像头设备"""
    print("正在检查可用的摄像头设备...")
    available_cameras = []

    # 检查前10个可能的摄像头索引
    for i in range(10):
        cap = cv2.VideoCapture(i)
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc("M", "J", "P", "G"))
        if cap.isOpened():
            ret, _ = cap.read()
            if ret:
                available_cameras.append(i)
                print(f"摄像头 {i}: 可用")
            cap.release()
            cv2.destroyAllWindows()  # 确保释放所有窗口资源

    import time

    time.sleep(1)  # 等待摄像头完全释放

    if not available_cameras:
        print("未找到任何可用的摄像头")
        return None

    print(f"\n找到 {len(available_cameras)} 个可用摄像头: {available_cameras}")
    return available_cameras


def run_tracking_system(model_path, camera_index, serial_port=None, baudrate=None):
    """运行云台跟踪系统"""
    try:
        from yolo_version.tracking_system import YOLOTrackingSystem

        print("\n启动云台跟踪系统...")
        print("=" * 60)
        print("功能说明:")
        print("  - 自动检测人耳并跟踪")
        print("  - 单目标检测与舵机控制")
        if ENABLE_DISPLAY:
            print("  - 键盘快捷键: 空格(暂停), q(退出)")
        else:
            print("  - 无显示模式: 不创建窗口，可节省资源")
        print("=" * 60)

        # 创建跟踪系统
        system = YOLOTrackingSystem(
            model_path=model_path,
            camera_index=camera_index,
            enable_visualization=ENABLE_DISPLAY,
            serial_port=serial_port,
            baudrate=baudrate,
        )

        # 启动系统
        system.start()

        return True

    except Exception as e:
        print(f"云台跟踪系统运行失败: {e}")
        import traceback

        traceback.print_exc()
        return False


def select_camera_with_timeout(available_cameras, timeout=10):
    """
    选择摄像头，支持超时自动选择第一个

    参数:
        available_cameras: 可用摄像头列表
        timeout: 超时时间（秒），默认10秒

    返回:
        选中的摄像头索引
    """
    import threading

    if len(available_cameras) == 1:
        camera_index = available_cameras[0]
        print(f"使用摄像头 {camera_index}")
        return camera_index

    print("\n请选择摄像头:")
    for i, cam_idx in enumerate(available_cameras):
        print(f"{i + 1}. 摄像头 {cam_idx}")

    selected_camera = [available_cameras[0]]
    input_received = [False]

    def get_input():
        try:
            choice = input(
                f"\n请输入选项 (1-{len(available_cameras)})，{timeout}秒后自动选择摄像头 {available_cameras[0]}: "
            )
            if choice.strip():
                cam_choice = int(choice)
                if 1 <= cam_choice <= len(available_cameras):
                    selected_camera[0] = available_cameras[cam_choice - 1]
            input_received[0] = True
        except (ValueError, EOFError):
            input_received[0] = True

    input_thread = threading.Thread(target=get_input, daemon=True)
    input_thread.start()
    input_thread.join(timeout=timeout)

    if not input_received[0]:
        print(f"\n超时，自动选择摄像头 {selected_camera[0]}")

    return selected_camera[0]


def find_model_path():
    """智能查找模型文件路径（优先使用config中的MODEL_PATH）"""
    # 优先使用 config.py 中指定的模型路径
    if MODEL_PATH:
        # 尝试相对路径
        if os.path.exists(MODEL_PATH):
            abs_path = os.path.abspath(MODEL_PATH)
            print(f"✓ 使用配置指定的模型: {abs_path}")
            return abs_path
        # 尝试从项目根路径
        project_root = os.path.abspath(os.path.dirname(__file__))
        from_root = os.path.join(project_root, MODEL_PATH)
        if os.path.exists(from_root):
            abs_path = os.path.abspath(from_root)
            print(f"✓ 使用配置指定的模型: {abs_path}")
            return abs_path

    # 只有当config中的模型不存在时，才查找备选
    print("! config.py中的模型路径不存在，检查备选模型...")
    possible_paths = [
        "models/best.pt",  # 首先查找.pt（遵守config优先级）
        os.path.join(os.path.dirname(__file__), "models/best.pt"),
        "models/best_int8.onnx",  # 再查找ONNX模型
        os.path.join(os.path.dirname(__file__), "models/best_int8.onnx"),
        "models/best.onnx",
        os.path.join(os.path.dirname(__file__), "models/best.onnx"),
        "best.onnx",
        "best.pt",
    ]

    for path in possible_paths:
        if os.path.exists(path):
            abs_path = os.path.abspath(path)
            print(f"✓ 找到模型文件: {abs_path}")
            return abs_path

    return None


def main():
    parser = argparse.ArgumentParser(description="YOLO云台人耳跟踪系统")
    parser.add_argument(
        "--serial-port",
        default=None,
        help="STM32 串口端口，例如 Windows COM3 或树莓派 /dev/serial0；不填则自动探测",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=RPI_STEPPER_SERIAL_BAUDRATE,
        help=f"STM32 串口波特率，默认 {RPI_STEPPER_SERIAL_BAUDRATE}",
    )
    args = parser.parse_args()

    print("\n" + "=" * 60)
    print("YOLO云台人耳跟踪系统")
    print("=" * 60)

    # 智能查找模型文件
    model_path = find_model_path()

    if not model_path:
        print("❌ 无法找到模型文件 best.pt")
        print("请确保模型文件在以下位置:")
        print("  - models/best.pt")
        print("  - 当前目录/best.pt")
        exit(1)

    # 检查可用摄像头
    available_cameras = check_available_cameras()
    if not available_cameras:
        print("❌ 未找到可用的摄像头")
        exit(1)

    # 选择摄像头（10秒超时自动选择第一个）
    camera_index = select_camera_with_timeout(available_cameras, timeout=10)

    # 启动云台跟踪模式
    return run_tracking_system(
        model_path,
        camera_index,
        serial_port=args.serial_port,
        baudrate=args.baudrate,
    )


if __name__ == "__main__":
    main()
