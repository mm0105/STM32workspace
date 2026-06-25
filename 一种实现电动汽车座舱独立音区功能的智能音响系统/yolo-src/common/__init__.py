"""
公共模块 - 树莓派人耳跟踪系统
基于步进电机UART控制的核心功能库
"""

__version__ = "1.0.0"
__author__ = "YOLO Step Motor Tracking System"

# 核心模块
from . import config
from . import visualization

__all__ = ["config", "serial_stepper_gimbal", "visualization"]
