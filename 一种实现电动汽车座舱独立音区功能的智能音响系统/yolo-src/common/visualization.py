"""
可视化管理模块
仅保留检测框显示功能
"""

import cv2

try:
    from .config import (
        COLOR_HIGH_CONF,
        COLOR_MED_CONF,
        COLOR_LOW_CONF,
        EAR_CLASS_ID,
    )
except ImportError:
    from config import (
        COLOR_HIGH_CONF,
        COLOR_MED_CONF,
        COLOR_LOW_CONF,
        EAR_CLASS_ID,
    )


class VisualizationManager:
    """可视化管理器 - 仅显示检测框"""

    def __init__(self):
        # 暂停状态
        self.is_paused = False

        # 键盘快捷键映射
        self.key_bindings = {
            ord(" "): "pause"  # 空格暂停
        }

    def handle_key(self, key):
        """
        处理键盘输入

        Args:
            key: 按键ASCII码

        Returns:
            action: 执行的动作
        """
        action = None

        if key in self.key_bindings:
            feature = self.key_bindings[key]

            if feature == "pause":
                self.is_paused = not self.is_paused
                action = "pause_toggled"

        return action

    def draw_all(self, frame, tracking_data=None):
        """
        绘制检测框

        Args:
            frame: 输入帧
            tracking_data: 跟踪数据

        Returns:
            output_frame: 添加了检测框的输出帧
        """
        output_frame = frame.copy()

        # 绘制检测框（人脸和耳朵）
        if tracking_data:
            self._draw_detections(output_frame, tracking_data)

        # 如果暂停，显示暂停指示
        if self.is_paused:
            cv2.putText(
                output_frame,
                "PAUSED",
                (frame.shape[1] // 2 - 50, 30),
                cv2.FONT_HERSHEY_SIMPLEX,
                1.0,
                (0, 0, 255),
                3,
            )

        return output_frame

    def _draw_detections(self, frame, tracking_data):
        """绘制检测框（仅耳朵）"""
        detections = tracking_data.get("detections", [])
        best_track = tracking_data.get("best_track")

        # 绘制屏幕中心十字线
        h, w = frame.shape[:2]
        center_x, center_y = w // 2, h // 2
        cv2.line(frame, (center_x - 20, center_y), (center_x + 20, center_y), (255, 255, 255), 2)
        cv2.line(frame, (center_x, center_y - 20), (center_x, center_y + 20), (255, 255, 255), 2)
        cv2.circle(frame, (center_x, center_y), 5, (255, 255, 255), -1)

        for det in detections:
            x1, y1, x2, y2 = det["box"]
            conf = det["conf"]
            cls = det["class_id"]

            # 选择颜色（只处理耳朵）
            if cls == EAR_CLASS_ID:
                if conf > 0.7:
                    color = COLOR_HIGH_CONF
                elif conf > 0.4:
                    color = COLOR_MED_CONF
                else:
                    color = COLOR_LOW_CONF
            else:
                continue

            # 绘制框
            cv2.rectangle(frame, (int(x1), int(y1)), (int(x2), int(y2)), color, 2)

            # 绘制标签
            label = f"ear:{conf:.2f}"

            label_size = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 2)[0]
            cv2.rectangle(
                frame,
                (int(x1), int(y1) - label_size[1] - 4),
                (int(x1) + label_size[0], int(y1)),
                color,
                -1,
            )
            cv2.putText(
                frame,
                label,
                (int(x1), int(y1) - 2),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.5,
                (0, 0, 0),
                1,
            )

        # 如果有跟踪目标，绘制目标中心点和瞄准线
        if best_track and best_track.lost_frames == 0:
            target_x, target_y = best_track.get_center()
            target_x, target_y = int(target_x), int(target_y)
            
            # 绘制目标中心点（红色圆圈）
            cv2.circle(frame, (target_x, target_y), 8, (0, 0, 255), 2)
            cv2.circle(frame, (target_x, target_y), 3, (0, 0, 255), -1)
            
            # 绘制从目标中心到屏幕中心的连线
            cv2.line(frame, (target_x, target_y), (center_x, center_y), (0, 255, 255), 2)
            
            # 显示偏移量
            offset_x = target_x - center_x
            offset_y = target_y - center_y
            offset_text = f"Offset: ({offset_x:+d}, {offset_y:+d})"
            cv2.putText(
                frame,
                offset_text,
                (10, h - 10),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.6,
                (0, 255, 255),
                2,
            )
