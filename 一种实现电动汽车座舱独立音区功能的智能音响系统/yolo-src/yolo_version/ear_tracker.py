"""
耳朵目标跟踪管理器

负责多目标检测下的目标选择、ID管理。
使用匈牙利算法进行最优 IOU 匹配，EMA 平滑目标位置。
"""

import math
import numpy as np
from scipy.optimize import linear_sum_assignment

from common.config import (
    MAX_LOST_FRAMES,
    IOU_MATCH_THRESHOLD,
    TARGET_SELECTION_MODE,
    WEIGHT_CONFIDENCE,
    WEIGHT_PROXIMITY,
    FRAME_WIDTH,
    FRAME_HEIGHT,
    EMA_SMOOTH_FACTOR,
)


def calculate_iou(box1, box2):
    """
    计算两个边界框的 IOU

    Args:
        box1: (x1, y1, x2, y2)
        box2: (x1, y1, x2, y2)

    Returns:
        IOU 值 [0, 1]
    """
    x1 = max(box1[0], box2[0])
    y1 = max(box1[1], box2[1])
    x2 = min(box1[2], box2[2])
    y2 = min(box1[3], box2[3])

    if x1 >= x2 or y1 >= y2:
        return 0.0

    inter = (x2 - x1) * (y2 - y1)
    area1 = (box1[2] - box1[0]) * (box1[3] - box1[1])
    area2 = (box2[2] - box2[0]) * (box2[3] - box2[1])
    union = area1 + area2 - inter

    return inter / union if union > 0 else 0.0


# 预计算画面对角线长度，用于距离归一化
_FRAME_DIAGONAL = math.hypot(FRAME_WIDTH / 2, FRAME_HEIGHT / 2)


class Track:
    """单个目标跟踪对象"""

    __slots__ = (
        "track_id",
        "box",
        "conf",
        "class_id",
        "center",
        "lost_frames",
        "is_active",
        "_ema_alpha",
    )

    def __init__(self, track_id, detection, ema_alpha=EMA_SMOOTH_FACTOR):
        self.track_id = track_id
        self.box = detection["box"]  # (x1, y1, x2, y2)
        self.conf = detection["conf"]
        self.class_id = detection["class_id"]
        self.center = self._calc_center(self.box)
        self.lost_frames = 0
        self.is_active = True
        self._ema_alpha = ema_alpha

    @staticmethod
    def _calc_center(box):
        return ((box[0] + box[2]) / 2, (box[1] + box[3]) / 2)

    def update(self, detection):
        """使用新检测结果更新跟踪，中心点用 EMA 平滑"""
        self.box = detection["box"]
        self.conf = detection["conf"]
        self.class_id = detection["class_id"]

        raw_center = self._calc_center(self.box)
        # EMA 平滑：减少目标抖动，低端设备检测帧率低时尤为重要
        a = self._ema_alpha
        self.center = (
            a * raw_center[0] + (1 - a) * self.center[0],
            a * raw_center[1] + (1 - a) * self.center[1],
        )

        self.lost_frames = 0
        self.is_active = True

    def mark_lost(self):
        """标记目标为丢失"""
        self.lost_frames += 1
        if self.lost_frames > MAX_LOST_FRAMES:
            self.is_active = False

    def get_center(self):
        """返回 EMA 平滑后的中心点"""
        return self.center

    def get_score(self, frame_center=None):
        """
        计算目标评分（用于多目标选择）

        Args:
            frame_center: 画面中心 (cx, cy)

        Returns:
            综合评分（越高越好）
        """
        if frame_center is None:
            frame_center = (FRAME_WIDTH / 2, FRAME_HEIGHT / 2)

        score = 0.0

        # 置信度分量
        if TARGET_SELECTION_MODE in ("confidence", "hybrid"):
            score += self.conf * WEIGHT_CONFIDENCE

        # 距离分量（距画面中心越近分数越高）
        if TARGET_SELECTION_MODE in ("proximity", "hybrid"):
            dx = self.center[0] - frame_center[0]
            dy = self.center[1] - frame_center[1]
            dist = math.hypot(dx, dy)
            prox_score = max(0.0, 1.0 - dist / _FRAME_DIAGONAL) * WEIGHT_PROXIMITY
            score += prox_score

        return score

    def get_size(self):
        """返回目标面积"""
        return (self.box[2] - self.box[0]) * (self.box[3] - self.box[1])


class EarTracker:
    """耳朵目标跟踪管理器（匈牙利算法 IOU 匹配）"""

    def __init__(self, match_threshold=IOU_MATCH_THRESHOLD):
        self.tracks = {}  # track_id -> Track
        self.next_track_id = 0
        self.match_threshold = match_threshold

    def update(self, detections):
        """
        使用新检测结果更新所有跟踪

        Args:
            detections: [{"box": (x1,y1,x2,y2), "conf": float, "class_id": int}, ...]

        Returns:
            活跃跟踪列表
        """
        if not detections:
            for track in self.tracks.values():
                track.mark_lost()
            return self.get_active_tracks()

        if not self.tracks:
            for det in detections:
                self._create_track(det)
            return self.get_active_tracks()

        # ---- 匈牙利算法最优匹配 ----
        track_ids = list(self.tracks.keys())
        n_tracks = len(track_ids)
        n_dets = len(detections)

        # 构建代价矩阵 (1 - IOU)，IOU 越大代价越小
        cost_matrix = np.ones((n_tracks, n_dets), dtype=np.float64)
        for i, tid in enumerate(track_ids):
            track = self.tracks[tid]
            if not track.is_active:
                continue  # 非活跃轨道代价保持为 1（最大）
            for j, det in enumerate(detections):
                iou = calculate_iou(track.box, det["box"])
                cost_matrix[i, j] = 1.0 - iou

        row_indices, col_indices = linear_sum_assignment(cost_matrix)

        matched_track_ids = set()
        matched_det_indices = set()

        for r, c in zip(row_indices, col_indices):
            iou = 1.0 - cost_matrix[r, c]
            if iou >= self.match_threshold:
                tid = track_ids[r]
                self.tracks[tid].update(detections[c])
                matched_track_ids.add(tid)
                matched_det_indices.add(c)

        # 未匹配的轨道标记丢失
        for tid in track_ids:
            if tid not in matched_track_ids:
                self.tracks[tid].mark_lost()

        # 未匹配的检测创建新轨道
        for j in range(n_dets):
            if j not in matched_det_indices:
                self._create_track(detections[j])

        return self.get_active_tracks()

    def _create_track(self, detection):
        """创建新跟踪"""
        self.tracks[self.next_track_id] = Track(self.next_track_id, detection)
        self.next_track_id += 1

    def get_active_tracks(self):
        """返回所有活跃轨道"""
        return [t for t in self.tracks.values() if t.is_active]

    def get_best_track(self, frame_center=None):
        """选择最佳跟踪目标"""
        active = self.get_active_tracks()
        if not active:
            return None
        if len(active) == 1:
            return active[0]
        return max(active, key=lambda t: t.get_score(frame_center))

    def remove_inactive_tracks(self):
        """移除不活跃的跟踪"""
        dead = [tid for tid, t in self.tracks.items() if not t.is_active]
        for tid in dead:
            del self.tracks[tid]

    def reset(self):
        """重置所有跟踪"""
        self.tracks.clear()
        self.next_track_id = 0
