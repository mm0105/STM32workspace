"""
树莓派人耳跟踪系统 - 公共配置文件
基于YOLO的人耳跟踪系统
"""

# =============================================================================
# 相机和分辨率配置
# =============================================================================
FRAME_WIDTH = 640
FRAME_HEIGHT = 480
FPS_TARGET = 30

# 显示设置
ENABLE_DISPLAY = True

# 摄像头翻转设置（摄像头倒装时设为True）
FLIP_VERTICAL = True
FLIP_HORIZONTAL = False

# =============================================================================
# YOLO检测参数
# =============================================================================
MODEL_PATH = "models/best.pt"
EAR_CLASS_ID = 1  # class 1: ear (人耳)
DETECTION_INTERVAL = 2  # 每帧检测

# =============================================================================
# 跟踪参数
# =============================================================================
MAX_LOST_FRAMES = 10
IOU_MATCH_THRESHOLD = 0.3

# =============================================================================
# 步进控制参数
# =============================================================================
DIRECTION_DEAD_ZONE = 15  # 死区（像素），误差小于此值不动作
STEP_MOVE_COOLDOWN_SECONDS = 0.05  # 冷却时间（秒），控制修正频率 ~20Hz
TRACKING_DAMPING_FACTOR = 0.3  # 微分阻尼系数（0~1），误差收敛时缩小步幅防超调

# 像素误差到步数映射
RPI_STEPPER_PIXELS_PER_STEP_X = 18.0
RPI_STEPPER_PIXELS_PER_STEP_Y = 18.0

# 单条命令步数上限
RPI_STEPPER_MAX_PAN_STEPS_PER_CMD = 120
RPI_STEPPER_MAX_TILT_STEPS_PER_CMD = 120

# 脉冲间隔（us）与机械稳定裕量（秒）
RPI_STEPPER_PULSE_US = 900
RPI_STEPPER_SETTLE_MARGIN_S = 0.03

# 单次命令角度上限（度）
RPI_STEPPER_SINGLE_MOVE_DEG = 1

# 串口下位机参数（PC/树莓派 -> STM32）
RPI_STEPPER_SERIAL_PORT = None  # None 表示自动探测；也可用 --serial-port 覆盖
RPI_STEPPER_SERIAL_BAUDRATE = 9600
RPI_STEPPER_SERIAL_ACK_TIMEOUT_S = 2.0
RPI_STEPPER_SERIAL_READ_TIMEOUT_S = 0.05
RPI_STEPPER_SERIAL_WRITE_TIMEOUT_S = 0.5
RPI_STEPPER_SERIAL_WINDOWS_PORTS = tuple(f"COM{i}" for i in range(1, 33))
RPI_STEPPER_SERIAL_RPI_PORTS = (
    "/dev/serial0",
    "/dev/ttyAMA0",
    "/dev/ttyUSB0",
    "/dev/ttyACM0",
)
RPI_STEPPER_SERIAL_EXTRA_PORTS = ()

# =============================================================================
# 云台参数（步进电机系统）
# =============================================================================
# 步进电机硬件参数
STEPPER_STEP_ANGLE = 1.8  # 整步角（度）
STEPPER_MICROSTEP = 32  # 微步分数（32分频）
STEPPER_EFFECTIVE_ANGLE = STEPPER_STEP_ANGLE / STEPPER_MICROSTEP

# 旧 GPIO 直驱映射（主流程已改为串口后端，保留给历史手动脚本参考）
RPI_STEPPER_USE_GPIO_DIRECT = False
RPI_STEPPER_GPIO_PAN_STEP = 12
RPI_STEPPER_GPIO_PAN_DIR = 24
RPI_STEPPER_GPIO_TILT_STEP = 16
RPI_STEPPER_GPIO_TILT_DIR = 23
RPI_STEPPER_GPIO_PAN_DIR_INVERT = False
RPI_STEPPER_GPIO_TILT_DIR_INVERT = False

# 机械零位与限位（单位：度）
PAN_STARTUP_ANGLE = 0.0
TILT_STARTUP_ANGLE = 0.0
TILT_BOOT_TARGET_ANGLE = -18.0  # 开机后自动移动到的工作角

PAN_RANGE = (-20.0, 20.0)
TILT_RANGE = (-28.0, 0.0)

# 计算有效步数范围
PAN_STEPS_RANGE = (
    int(round((PAN_RANGE[0] - PAN_STARTUP_ANGLE) / STEPPER_EFFECTIVE_ANGLE)),
    int(round((PAN_RANGE[1] - PAN_STARTUP_ANGLE) / STEPPER_EFFECTIVE_ANGLE)),
)
TILT_STEPS_RANGE = (
    int(round((TILT_RANGE[0] - TILT_STARTUP_ANGLE) / STEPPER_EFFECTIVE_ANGLE)),
    int(round((TILT_RANGE[1] - TILT_STARTUP_ANGLE) / STEPPER_EFFECTIVE_ANGLE)),
)

# 目标丢失后归位参数
TARGET_LOST_RETURN_HOME_SECONDS = 5.0

# =============================================================================
# 可视化参数
# =============================================================================
COLOR_HIGH_CONF = (0, 255, 0)   # 高置信度（绿色）
COLOR_MED_CONF = (0, 255, 255)  # 中等置信度（黄色）
COLOR_LOW_CONF = (0, 0, 255)     # 低置信度（红色）

# =============================================================================
# 目标选择策略
# =============================================================================
TARGET_SELECTION_MODE = "hybrid"  # "confidence" | "proximity" | "hybrid"
WEIGHT_CONFIDENCE = 0.5
WEIGHT_PROXIMITY = 0.5

# EMA 平滑参数
EMA_SMOOTH_FACTOR = 0.7

# =============================================================================
# 音频功能
# =============================================================================
ENABLE_SPEAKER = True  # 设置为 True 以开启喇叭循环播放音乐
SPEAKER_MUSIC_PATH = "models/mc.mp3"
SPEAKER_VOLUME = 0.35  # 喇叭音量（0.0-1.0），默认50%
SPEAKER_AUDIO_DRIVER = "auto"  # "auto" | "alsa" | "directsound" | "dummy"
SPEAKER_ALSA_DEVICE = "plughw:2,0"
