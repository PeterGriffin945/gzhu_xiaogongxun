"""纯视觉逻辑：调用 CanMV image.find_blobs，但不导入任何硬件模块。"""

try:
    from config import (
        COLOR_SEARCH_ORDER,
        LAB_THRESHOLDS,
        STABLE_FRAME_COUNT,
        VISION_PROFILES,
    )
    from protocol import (
        COLOR_UNKNOWN,
        MODE_MATERIAL,
        MODE_RING,
    )
except ImportError:
    from .config import (
        COLOR_SEARCH_ORDER,
        LAB_THRESHOLDS,
        STABLE_FRAME_COUNT,
        VISION_PROFILES,
    )
    from .protocol import (
        COLOR_UNKNOWN,
        MODE_MATERIAL,
        MODE_RING,
    )


class DetectionResult:
    __slots__ = (
        "mode",
        "color",
        "detected",
        "stable",
        "cx",
        "cy",
        "area",
        "confidence",
    )

    def __init__(self, mode, color=COLOR_UNKNOWN, detected=False,
                 stable=False, cx=0, cy=0, area=0, confidence=0):
        self.mode = mode
        self.color = color
        self.detected = detected
        self.stable = stable
        self.cx = cx
        self.cy = cy
        self.area = area
        self.confidence = confidence


def empty_detection(mode):
    return DetectionResult(mode)


class StableColorVote:
    """只有连续 N 帧为同一颜色才置 stable；丢帧或换色立即重新计数。"""

    def __init__(self, required_count=STABLE_FRAME_COUNT):
        self.required_count = required_count
        self.reset()

    def reset(self):
        self.candidate_color = COLOR_UNKNOWN
        self.consecutive_count = 0

    def update(self, color):
        if color == COLOR_UNKNOWN:
            self.reset()
            return False

        if color != self.candidate_color:
            self.candidate_color = color
            self.consecutive_count = 1
        else:
            self.consecutive_count = min(
                self.required_count,
                self.consecutive_count + 1,
            )
        return self.consecutive_count >= self.required_count


class VisionProcessor:
    def __init__(self):
        self.vote = StableColorVote()

    def reset(self):
        self.vote.reset()

    def process(self, source_image, mode):
        """返回最大有效 blob；blob 坐标始终是整幅图像坐标。"""
        if mode not in (MODE_MATERIAL, MODE_RING):
            self.reset()
            return empty_detection(mode)

        profile = VISION_PROFILES[mode]
        best_color = COLOR_UNKNOWN
        best_blob = None
        best_area = 0

        # 每种颜色单独查找，避免多色阈值合并后丢失颜色身份。
        for color in COLOR_SEARCH_ORDER:
            blobs = source_image.find_blobs(
                [LAB_THRESHOLDS[color]],
                roi=profile["roi"],
                area_threshold=profile["min_area"],
                pixels_threshold=profile["pixels_threshold"],
                merge=False,
            )
            if not blobs:
                continue

            # 先按面积排序，再在最大目标数限制内选择，避免漏掉后返回的大目标。
            ordered_blobs = sorted(
                blobs,
                key=lambda blob: blob.area(),
                reverse=True,
            )
            for blob in ordered_blobs[:profile["max_blobs"]]:
                area = int(blob.area())
                pixels = int(blob.pixels())
                if area < profile["min_area"]:
                    continue
                if pixels < profile["pixels_threshold"]:
                    continue
                if area > best_area:
                    best_area = area
                    best_color = color
                    best_blob = blob

        if best_blob is None:
            self.vote.update(COLOR_UNKNOWN)
            return empty_detection(mode)

        stable = self.vote.update(best_color)
        area = max(1, int(best_blob.area()))
        pixels = max(0, int(best_blob.pixels()))

        # 置信度仅反映着色密度和相对面积，不代表测量或识别的物理准确率。
        density = min(1.0, pixels / float(area))
        area_factor = min(1.0, area / float(profile["min_area"] * 4))
        confidence = max(0, min(100, int(100 * density * area_factor)))

        return DetectionResult(
            mode=mode,
            color=best_color,
            detected=True,
            stable=stable,
            cx=int(best_blob.cx()),
            cy=int(best_blob.cy()),
            area=area,
            confidence=confidence,
        )
