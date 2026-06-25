"""YOLO 人耳跟踪系统（树莓派步进UART重构版）"""

import os
import sys
import time
import cv2

from ultralytics import YOLO

# 允许从项目根导入 common/
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from common.config import (
    MODEL_PATH,
    IOU_MATCH_THRESHOLD,
    FRAME_WIDTH,
    FRAME_HEIGHT,
    FPS_TARGET,
    ENABLE_DISPLAY,
    ENABLE_SPEAKER,
    SPEAKER_MUSIC_PATH,
    SPEAKER_VOLUME,
    SPEAKER_AUDIO_DRIVER,
    SPEAKER_ALSA_DEVICE,
    FLIP_VERTICAL,
    FLIP_HORIZONTAL,
    DETECTION_INTERVAL,
    MAX_LOST_FRAMES,
    EAR_CLASS_ID,
    TARGET_LOST_RETURN_HOME_SECONDS,
    DIRECTION_DEAD_ZONE,
    STEP_MOVE_COOLDOWN_SECONDS,
    TRACKING_DAMPING_FACTOR,
    RPI_STEPPER_MAX_PAN_STEPS_PER_CMD,
    RPI_STEPPER_MAX_TILT_STEPS_PER_CMD,
    RPI_STEPPER_PIXELS_PER_STEP_X,
    RPI_STEPPER_PIXELS_PER_STEP_Y,
    RPI_STEPPER_SERIAL_PORT,
    RPI_STEPPER_SERIAL_BAUDRATE,
    RPI_STEPPER_SERIAL_ACK_TIMEOUT_S,
    RPI_STEPPER_SERIAL_READ_TIMEOUT_S,
    RPI_STEPPER_SERIAL_WRITE_TIMEOUT_S,
    RPI_STEPPER_SERIAL_WINDOWS_PORTS,
    RPI_STEPPER_SERIAL_RPI_PORTS,
    RPI_STEPPER_SERIAL_EXTRA_PORTS,
    PAN_STEPS_RANGE,
    TILT_STEPS_RANGE,
    STEPPER_EFFECTIVE_ANGLE,
    RPI_STEPPER_SINGLE_MOVE_DEG,
    PAN_STARTUP_ANGLE,
    TILT_BOOT_TARGET_ANGLE,
)
from common.serial_stepper_gimbal import SerialStepperGimbal
from common.visualization import VisualizationManager
from .ear_tracker import EarTracker


class YOLOTrackingSystem:
    """主跟踪系统"""

    def __init__(
        self,
        model_path=MODEL_PATH,
        camera_index=0,
        enable_visualization=ENABLE_DISPLAY,
        serial_port=None,
        baudrate=None,
    ):
        print("=" * 60)
        print("云台人耳跟踪系统初始化")
        print("=" * 60)

        self.model = YOLO(model_path)
        self.model_classes = [EAR_CLASS_ID]

        self.gimbal = SerialStepperGimbal(
            serial_port=serial_port or RPI_STEPPER_SERIAL_PORT,
            baudrate=baudrate or RPI_STEPPER_SERIAL_BAUDRATE,
            max_pan_steps=RPI_STEPPER_MAX_PAN_STEPS_PER_CMD,
            max_tilt_steps=RPI_STEPPER_MAX_TILT_STEPS_PER_CMD,
            ack_timeout_s=RPI_STEPPER_SERIAL_ACK_TIMEOUT_S,
            read_timeout_s=RPI_STEPPER_SERIAL_READ_TIMEOUT_S,
            write_timeout_s=RPI_STEPPER_SERIAL_WRITE_TIMEOUT_S,
            windows_ports=RPI_STEPPER_SERIAL_WINDOWS_PORTS,
            rpi_ports=RPI_STEPPER_SERIAL_RPI_PORTS,
            extra_ports=RPI_STEPPER_SERIAL_EXTRA_PORTS,
        )

        # 步进控制参数
        self.direction_dead_zone = max(0.0, float(DIRECTION_DEAD_ZONE))
        self.step_move_cooldown_seconds = max(0.01, float(STEP_MOVE_COOLDOWN_SECONDS))
        self.pixels_per_step_x = max(0.1, float(RPI_STEPPER_PIXELS_PER_STEP_X))
        self.pixels_per_step_y = max(0.1, float(RPI_STEPPER_PIXELS_PER_STEP_Y))
        self.pan_min_steps, self.pan_max_steps = sorted(
            (int(PAN_STEPS_RANGE[0]), int(PAN_STEPS_RANGE[1]))
        )
        self.tilt_min_steps, self.tilt_max_steps = sorted(
            (int(TILT_STEPS_RANGE[0]), int(TILT_STEPS_RANGE[1]))
        )
        self.single_move_deg = max(0.01, float(RPI_STEPPER_SINGLE_MOVE_DEG))
        self.single_move_max_steps = max(
            1, int(round(self.single_move_deg / max(1e-9, STEPPER_EFFECTIVE_ANGLE)))
        )
        self._tracking_damping = max(0.0, min(1.0, float(TRACKING_DAMPING_FACTOR)))

        # 本次启动的逻辑累计位置（防止外部读数异常导致越界）
        self._logical_pan_steps = 0
        self._logical_tilt_steps = 0

        self._prev_error = None  # 上一帧误差，用于微分阻尼
        self._last_step_move_time = 0.0

        self.tracker = EarTracker(match_threshold=IOU_MATCH_THRESHOLD)
        self.enable_display = enable_visualization
        self.visualization = VisualizationManager() if self.enable_display else None

        self.camera_index = camera_index
        self.frame_center = (FRAME_WIDTH / 2, FRAME_HEIGHT / 2)

        # FPS 统计
        self.frame_count = 0
        self.fps_counter = 0
        self.fps_start_time = time.time()
        self.current_fps = 0.0

        # 目标状态
        self.best_track = None
        self.best_detection = None
        self.target_type = "none"
        self.lost_frames = 0
        self.last_target_seen_time = time.time()
        self._has_returned_home_after_timeout = False

        print(f"模型路径: {model_path}")
        print(f"相机设置: {FRAME_WIDTH}x{FRAME_HEIGHT} @ {FPS_TARGET}FPS")
        print(f"显示输出: {'开启' if self.enable_display else '关闭'}")
        print(
            "云台控制: 串口 -> STM32步进下位机"
            f"(baudrate={self.gimbal.baudrate}, single-flight异步)"
        )
        print("初始化完成")

    # ------------------------------------------------------------------
    # 云台初始化
    # ------------------------------------------------------------------

    def _initialize_gimbal_position(self):
        """将云台移动到开机工作位置（PAN_STARTUP_ANGLE, TILT_BOOT_TARGET_ANGLE）"""
        try:
            # 计算开机工作位置对应的步数（相对于当前逻辑零点）
            pan_steps = int(round(PAN_STARTUP_ANGLE / STEPPER_EFFECTIVE_ANGLE))
            tilt_steps = int(round(TILT_BOOT_TARGET_ANGLE / STEPPER_EFFECTIVE_ANGLE))

            # 应用机械限位
            pan_steps = max(self.pan_min_steps, min(self.pan_max_steps, pan_steps))
            tilt_steps = max(self.tilt_min_steps, min(self.tilt_max_steps, tilt_steps))

            print(f"  [Pan] 启动角: {PAN_STARTUP_ANGLE}° → {pan_steps} steps")
            print(
                f"  [Tilt] 开机工作角: {TILT_BOOT_TARGET_ANGLE}° → {tilt_steps} steps"
            )

            # 执行云台初始化移动（此时gimbal.start()已被调用）
            if pan_steps != 0 or tilt_steps != 0:
                success = self.gimbal.enqueue_step_move(pan_steps, tilt_steps)
                if success:
                    print("  ✓ 云台开机工作位置命令已发送")
                else:
                    print("  ⚠ 云台命令被拒绝（单飞中）")
            else:
                print("  ✓ 云台已在开机工作位置(0,0)")

            self._logical_pan_steps = pan_steps
            self._logical_tilt_steps = tilt_steps
        except Exception as e:
            print(f"  ⚠ 云台启动初始化异常: {e}")
            import traceback

            traceback.print_exc()

    def _initialize_speaker(self):
        """初始化喇叭音频播放"""
        import os
        import platform
        import sys

        music_path = SPEAKER_MUSIC_PATH
        if not os.path.isabs(music_path):
            music_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", music_path))

        if not os.path.exists(music_path):
            print(f"❌ 喇叭音乐文件未找到: {music_path}")
            return

        os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
        audio_driver = str(SPEAKER_AUDIO_DRIVER).strip().lower()
        if audio_driver == "auto":
            audio_driver = "alsa" if platform.system().lower() == "linux" else ""

        if audio_driver:
            os.environ["SDL_AUDIODRIVER"] = audio_driver
            if audio_driver == "alsa" and SPEAKER_ALSA_DEVICE:
                os.environ["AUDIODEV"] = str(SPEAKER_ALSA_DEVICE)
        else:
            os.environ.pop("SDL_AUDIODRIVER", None)
            os.environ.pop("AUDIODEV", None)

        # 如果 pygame 已经被导入，需要重新加载
        if 'pygame' in sys.modules:
            del sys.modules['pygame']
            if 'pygame.mixer' in sys.modules:
                del sys.modules['pygame.mixer']

        try:
            import pygame
            pygame.mixer.quit()
            pygame.mixer.init(frequency=44100, size=-16, channels=2, buffer=2048)

            pygame.mixer.music.load(music_path)
            pygame.mixer.music.set_volume(SPEAKER_VOLUME)
            pygame.mixer.music.play(loops=-1)

            driver_label = audio_driver or "pygame默认"
            print(f"✓ 喇叭已启动（音频驱动: {driver_label}）")
            print(f"  音乐文件: {music_path}")
            print(f"  音量: {int(SPEAKER_VOLUME * 100)}%")
        except Exception as e:
            print(f"⚠ 喇叭初始化失败，已跳过音频播放: {e}")

    # ------------------------------------------------------------------
    # 主循环
    # ------------------------------------------------------------------

    def start(self):
        """启动主循环"""
        self.gimbal.start()
        print(
            f"[初始化] 云台串口启动成功: {self.gimbal.active_port}, "
            "同步初始零点..."
        )
        self._initialize_gimbal_position()

        print("\n启动摄像头...")
        cv2.destroyAllWindows()

        cap = cv2.VideoCapture(self.camera_index)
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc("M", "J", "P", "G"))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, FRAME_WIDTH)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, FRAME_HEIGHT)
        cap.set(cv2.CAP_PROP_FPS, FPS_TARGET)

        if not cap.isOpened():
            print(f"无法打开摄像头 {self.camera_index}")
            self.gimbal.stop()
            cap.release()
            cv2.destroyAllWindows()
            return False

        actual_w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        actual_h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        print(f"摄像头启动成功: {actual_w}x{actual_h}")

        # 用实际摄像头分辨率更新 frame_center，避免配置与实际不符导致偏移
        if actual_w != FRAME_WIDTH or actual_h != FRAME_HEIGHT:
            print(f"⚠ 实际分辨率 {actual_w}x{actual_h} 与配置 {FRAME_WIDTH}x{FRAME_HEIGHT} 不一致，")
            print(f"  frame_center 已从 ({FRAME_WIDTH/2:.0f}, {FRAME_HEIGHT/2:.0f}) "
                  f"更新为 ({actual_w/2:.0f}, {actual_h/2:.0f})")
        self.frame_center = (actual_w / 2, actual_h / 2)

        window_name = "YOLO Ear Tracking"
        if self.enable_display:
            cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)
            cv2.resizeWindow(window_name, FRAME_WIDTH, FRAME_HEIGHT)
            print("按 q 退出，空格暂停")
        else:
            print("无显示模式运行中（生产环境省资源）")

        # 初始化喇叭（如果启用）
        if ENABLE_SPEAKER:
            self._initialize_speaker()

        try:
            while True:
                ret, frame = cap.read()
                if not ret:
                    print("无法读取摄像头帧")
                    break

                if FLIP_VERTICAL:
                    frame = cv2.flip(frame, 0)
                if FLIP_HORIZONTAL:
                    frame = cv2.flip(frame, 1)

                # 暂停模式
                if (
                    self.enable_display
                    and self.visualization
                    and self.visualization.is_paused
                ):
                    cv2.imshow(window_name, frame)
                    key = cv2.waitKey(30) & 0xFF
                    if key == ord("q"):
                        break
                    if self.visualization:
                        self.visualization.handle_key(key)
                    continue

                output = self.process_frame(frame)

                if self.enable_display:
                    cv2.imshow(window_name, output)
                    key = cv2.waitKey(1) & 0xFF
                    if self.visualization:
                        self.visualization.handle_key(key)
                    if key == ord("q"):
                        break

        finally:
            self.gimbal.stop()
            cap.release()
            cv2.destroyAllWindows()

        return True

    # ------------------------------------------------------------------
    # 帧处理管线
    # ------------------------------------------------------------------

    def process_frame(self, frame):
        """处理单帧：检测 -> 跟踪 -> 控制 -> 可视化"""
        self.frame_count += 1
        self._update_fps()

        run_detection = (
            DETECTION_INTERVAL <= 1
            or self.frame_count % DETECTION_INTERVAL == 0
            or self.best_track is None
        )

        detections = self._detect_ears(frame) if run_detection else []

        if run_detection:
            # 仅保留最高置信度目标，避免多目标切换干扰步进电机
            self.best_detection = (
                max(detections, key=lambda d: d["conf"]) if detections else None
            )
            single = [self.best_detection] if self.best_detection else []

            self.tracker.update(single)
            self.tracker.remove_inactive_tracks()
            self.best_track = self.tracker.get_best_track(self.frame_center)

            if self.best_track:
                if self.target_type != "real":
                    self._reset_lost_target_state()
                self.target_type = "real"
                self.lost_frames = 0
                self.last_target_seen_time = time.time()
            else:
                self.target_type = "none"
                self.lost_frames = min(self.lost_frames + 1, MAX_LOST_FRAMES)

        # 仅在当前帧命中目标时才执行云台控制
        has_fresh_target = (
            self.best_track is not None and self.best_track.lost_frames == 0
        )

        if has_fresh_target:
            step_cmd = self._get_directional_step_command(self.best_track)
            if step_cmd is not None:
                pan_steps, tilt_steps = step_cmd
                accepted = self.gimbal.enqueue_step_move(pan_steps, tilt_steps)
                if accepted:
                    self._last_step_move_time = time.time()
                    self._logical_pan_steps = max(
                        self.pan_min_steps,
                        min(
                            self.pan_max_steps, self._logical_pan_steps + int(pan_steps)
                        ),
                    )
                    self._logical_tilt_steps = max(
                        self.tilt_min_steps,
                        min(
                            self.tilt_max_steps,
                            self._logical_tilt_steps + int(tilt_steps),
                        ),
                    )
        else:
            self._search_target_if_lost_too_long()

        if not self.visualization:
            return frame

        tracking_data = {
            "detections": detections,
            "best_track": self.best_track,
            "target_type": self.target_type,
            "fps": self.current_fps,
            "mode": f"rpi-stepper-{self.gimbal.mode}",
            "lost_frames": self.lost_frames,
        }
        return self.visualization.draw_all(frame, tracking_data=tracking_data)

    # ------------------------------------------------------------------
    # 检测
    # ------------------------------------------------------------------

    def _detect_ears(self, frame):
        """执行耳朵检测，批量转换张量，减少逐元素开销"""
        results = self.model(
            frame,
            classes=self.model_classes,
            verbose=False,
        )

        result = results[0] if results else None
        if result is None or result.boxes is None or len(result.boxes) == 0:
            return []

        boxes = result.boxes
        # 批量取出所有数据到 numpy，避免逐元素 .cpu().numpy()
        xyxy = boxes.xyxy.cpu().numpy()
        confs = boxes.conf.cpu().numpy()
        classes = boxes.cls.cpu().numpy().astype(int)

        detections = []
        for i in range(len(boxes)):
            if classes[i] != EAR_CLASS_ID:
                continue
            detections.append(
                {
                    "box": tuple(xyxy[i].tolist()),
                    "conf": float(confs[i]),
                    "class_id": int(classes[i]),
                }
            )
        return detections

    # ------------------------------------------------------------------
    # 方向步进控制
    # ------------------------------------------------------------------

    def _get_directional_step_command(self, track):
        """比例步进控制 + 微分阻尼：平滑跟踪，抑制振荡"""
        target_x, target_y = track.get_center()
        error_x = target_x - self.frame_center[0]
        error_y = target_y - self.frame_center[1]

        # 死区内不动作
        if abs(error_x) <= self.direction_dead_zone and abs(error_y) <= self.direction_dead_zone:
            self._prev_error = (error_x, error_y)
            return None

        # 冷却检查（EMA 已滤除检测噪声，无需多帧稳定门控）
        now = time.time()
        if (now - self._last_step_move_time) < self.step_move_cooldown_seconds:
            return None

        if not self.gimbal.can_accept_command():
            return None

        # 比例映射：像素误差 → 步数
        pan_steps = 0
        tilt_steps = 0

        if abs(error_x) > self.direction_dead_zone:
            raw = -error_x / self.pixels_per_step_x
            pan_steps = int(round(raw)) or (-1 if error_x > 0 else 1)

        if abs(error_y) > self.direction_dead_zone:
            raw = error_y / self.pixels_per_step_y
            tilt_steps = int(round(raw)) or (1 if error_y > 0 else -1)

        # 微分阻尼：误差在收敛时缩小步幅，防止超调振荡
        if self._prev_error is not None and self._tracking_damping > 0:
            d_ex = error_x - self._prev_error[0]
            d_ey = error_y - self._prev_error[1]
            scale = 1.0 - self._tracking_damping  # 0.7 即在收敛时只发 70% 步
            # 误差与变化量异号 = 正在收敛 → 减小步幅
            if error_x * d_ex < 0 and pan_steps != 0:
                damped = int(round(pan_steps * scale))
                pan_steps = damped or (1 if pan_steps > 0 else -1)
            if error_y * d_ey < 0 and tilt_steps != 0:
                damped = int(round(tilt_steps * scale))
                tilt_steps = damped or (1 if tilt_steps > 0 else -1)

        self._prev_error = (error_x, error_y)

        if pan_steps == 0 and tilt_steps == 0:
            return None

        if FLIP_HORIZONTAL:
            pan_steps = -pan_steps

        # 钳制到命令步数上限
        pan_steps = max(
            -RPI_STEPPER_MAX_PAN_STEPS_PER_CMD,
            min(RPI_STEPPER_MAX_PAN_STEPS_PER_CMD, pan_steps),
        )
        tilt_steps = max(
            -RPI_STEPPER_MAX_TILT_STEPS_PER_CMD,
            min(RPI_STEPPER_MAX_TILT_STEPS_PER_CMD, tilt_steps),
        )

        # 单次移动角度限制
        pan_steps = max(
            -self.single_move_max_steps,
            min(self.single_move_max_steps, pan_steps),
        )
        tilt_steps = max(
            -self.single_move_max_steps,
            min(self.single_move_max_steps, tilt_steps),
        )

        # 机械限位
        pan_steps, tilt_steps = self._clamp_step_command_to_limits(
            pan_steps, tilt_steps
        )
        if pan_steps == 0 and tilt_steps == 0:
            return None

        return pan_steps, tilt_steps

    def _clamp_step_command_to_limits(self, pan_steps, tilt_steps):
        """按机械绝对限位钳制命令，超限部分直接截断。"""
        cur_pan, cur_tilt = self._logical_pan_steps, self._logical_tilt_steps

        target_pan = cur_pan + int(pan_steps)
        target_tilt = cur_tilt + int(tilt_steps)

        clamped_target_pan = max(
            self.pan_min_steps, min(self.pan_max_steps, target_pan)
        )
        clamped_target_tilt = max(
            self.tilt_min_steps, min(self.tilt_max_steps, target_tilt)
        )

        return clamped_target_pan - cur_pan, clamped_target_tilt - cur_tilt

    # ------------------------------------------------------------------
    # 辅助
    # ------------------------------------------------------------------

    def _update_fps(self):
        self.fps_counter += 1
        now = time.time()
        elapsed = now - self.fps_start_time
        if elapsed < 1.0:
            return

        self.current_fps = self.fps_counter / elapsed
        self.fps_counter = 0
        self.fps_start_time = now

        conf = self.best_track.conf if self.best_track else 0.0
        print(
            f"[FPS] {self.current_fps:.1f} | "
            f"[Conf] {conf:.2f} | "
            f"[Target] {self.target_type} | "
            f"[Lost] {self.lost_frames}/{MAX_LOST_FRAMES}",
            end="\r",
        )

    def _search_target_if_lost_too_long(self):
        """目标连续丢失超时后归位一次"""
        lost_seconds = time.time() - self.last_target_seen_time
        if lost_seconds < TARGET_LOST_RETURN_HOME_SECONDS:
            return
        if self._has_returned_home_after_timeout:
            return

        try:
            if not self.gimbal.can_accept_command():
                return

            home_pan_steps = int(round(PAN_STARTUP_ANGLE / STEPPER_EFFECTIVE_ANGLE))
            home_tilt_steps = int(
                round(TILT_BOOT_TARGET_ANGLE / STEPPER_EFFECTIVE_ANGLE)
            )
            home_pan_steps = max(
                self.pan_min_steps, min(self.pan_max_steps, home_pan_steps)
            )
            home_tilt_steps = max(
                self.tilt_min_steps, min(self.tilt_max_steps, home_tilt_steps)
            )

            pan_pos, tilt_pos = self._logical_pan_steps, self._logical_tilt_steps
            if pan_pos == home_pan_steps and tilt_pos == home_tilt_steps:
                self._has_returned_home_after_timeout = True
                return

            accepted = self.gimbal.enqueue_step_move(
                home_pan_steps - pan_pos,
                home_tilt_steps - tilt_pos,
            )
            if not accepted:
                return

            self._logical_pan_steps = home_pan_steps
            self._logical_tilt_steps = home_tilt_steps

            self._has_returned_home_after_timeout = True
            print(
                f"\n[PTZ] 目标丢失超过 {TARGET_LOST_RETURN_HOME_SECONDS:.1f}s，已自动归位到开机工作位"
            )
        except Exception as e:
            print(f"\n[PTZ] 丢失后归位失败: {e}")

    def _reset_lost_target_state(self):
        self._has_returned_home_after_timeout = False
        self._prev_error = None

    def get_performance_stats(self):
        pan_steps, tilt_steps = self.gimbal.get_position_steps()
        return {
            "fps": self.current_fps,
            "target_type": self.target_type,
            "lost_frames": self.lost_frames,
            "active_tracks": len(self.tracker.get_active_tracks()),
            "pan_steps": pan_steps,
            "tilt_steps": tilt_steps,
        }
