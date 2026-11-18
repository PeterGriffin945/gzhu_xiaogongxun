# 正点原子 DNK230D（K230D BOX）视觉节点
#
# 参考官方 CanMV 例程的接口写法：
#   基础实验 8  UART   ：FPIOA + UART(UART.UART1)（GPIO40 TX / GPIO41 RX）
#   基础实验 10 LCD    ：Display.ST7701 640x480
#   基础实验 12 摄像头 ：Sensor(width=1280, height=960) + Sensor.VGA + RGB565
#   图像 11 色块追踪   ：find_blobs + rect/cx/cy
#
# 功能：
#   1. 640x480 板载屏实时显示摄像头画面、ROI、准星、物料框与偏移量
#   2. 通过 PORT1（UART1，GPIO40/41，115200 8N1）与 ESP32-S3 通信
#   3. 协议：A5 5A + ver/type/seq/len + payload + CRC8（CRC 不含同步字）
#      命令 COMMAND / ACK / 心跳 / 80ms 周期 DETECTION（绝对坐标）
#   4. MATERIAL / RING 分区 ROI；IDLE 时做预览检测但不向 ESP 上报
#
# 接线：K230 GPIO40(TX) → ESP GPIO44(RX)，K230 GPIO41(RX) ← ESP GPIO43(TX)，共地。
import time
import math
import struct
import os

from machine import UART
from machine import FPIOA

from media.sensor import Sensor
from media.display import Display
from media.media import MediaManager

# ========================= 可调参数 =========================
FRAME_W = 640
FRAME_H = 480
CENTER_X = 320           # VGA 画面中心，也是 ESP 的默认目标点（可被 mcx/mcy 覆盖）
CENTER_Y = 240
ALIGN_RADIUS = 20        # 屏幕上的对准提示半径（最终由 ESP 容差判断）

DETECTION_MS = 80        # 12.5 Hz
HEARTBEAT_MS = 500
STABLE_FRAMES = 3
STRICT_ROI = False       # True = 只认 ROI；False = ROI 无目标时回退全图

MODE_IDLE, MODE_MATERIAL, MODE_RING, MODE_TRAY = 0, 1, 2, 3
COLOR_UNKNOWN, COLOR_RED, COLOR_GREEN, COLOR_BLUE = 0, 1, 2, 3
TYPE_COMMAND, TYPE_HEARTBEAT, TYPE_ACK, TYPE_DETECTION = 0x01, 0x02, 0x03, 0x10

# ROI 为 VGA(640x480) 坐标 (x, y, w, h)
PROFILES = {
    MODE_MATERIAL: {"roi": (0, 70, 640, 340), "min_pixels": 400, "min_area": 400},
    MODE_RING:     {"roi": (40, 20, 560, 440), "min_pixels": 300, "min_area": 300},
}

# LAB 阈值，必须按现场光照重标
THRESHOLDS = [
    (41, 99, -28, 24, 24, 91),     # 红
    (30, 80, -60, -15, 5, 50),     # 绿
    (20, 70, -10, 30, -60, -10),   # 蓝
]

COLOR_STYLES = {
    COLOR_RED:   ("RED", (255, 50, 50)),
    COLOR_GREEN: ("GREEN", (50, 255, 50)),
    COLOR_BLUE:  ("BLUE", (50, 150, 255)),
}


# ========================= 协议层 =========================
def crc8(data):
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def build_frame(msg_type, seq, payload):
    body = bytes((0x01, msg_type, seq & 0xFF, len(payload))) + payload
    return b"\xA5\x5A" + body + bytes((crc8(body),))


_rx = b""


def parse_frames(data):
    global _rx
    frames = []
    if data:
        _rx += data
    while True:
        index = _rx.find(b"\xA5\x5A")
        if index < 0:
            _rx = b"\xA5" if _rx[-1:] == b"\xA5" else b""
            break
        if index:
            _rx = _rx[index:]
        if len(_rx) < 6:
            break
        length = _rx[5]
        if _rx[2] != 0x01 or length > 32:
            _rx = _rx[1:]
            continue
        total = length + 7
        if len(_rx) < total:
            break
        if crc8(_rx[2:total - 1]) != _rx[total - 1]:
            _rx = _rx[1:]
            continue
        frames.append((_rx[3], _rx[4], bytes(_rx[6:total - 1])))
        _rx = _rx[total:]
    return frames


def code_to_color(code):
    if code & 0x01:
        return COLOR_RED
    if code & 0x02:
        return COLOR_GREEN
    if code & 0x04:
        return COLOR_BLUE
    return COLOR_UNKNOWN


def detect_blobs(img, profile, use_roi):
    if use_roi:
        try:
            return img.find_blobs(
                THRESHOLDS,
                roi=profile["roi"],
                pixels_threshold=profile["min_pixels"],
                area_threshold=profile["min_area"],
                merge=True,
            )
        except Exception:
            pass
    return img.find_blobs(
        THRESHOLDS,
        pixels_threshold=profile["min_pixels"],
        area_threshold=profile["min_area"],
        merge=True,
    )


# ========================= 硬件初始化（官方例程写法） =========================
# PORT1：GPIO40=UART1_TXD，GPIO41=UART1_RXD
fpioa = FPIOA()
fpioa.set_function(40, FPIOA.UART1_TXD)
fpioa.set_function(41, FPIOA.UART1_RXD)

uart = UART(UART.UART1, baudrate=115200,
            bits=UART.EIGHTBITS, parity=UART.PARITY_NONE,
            stop=UART.STOPBITS_ONE)

# 摄像头：官方例程用 Sensor(width=1280, height=960) + VGA 输出
sensor = Sensor(width=1280, height=960)
sensor.reset()
sensor.set_framesize(Sensor.VGA)        # 640x480
sensor.set_pixformat(Sensor.RGB565)

# 板载屏：ST7701 640x480，90fps，同时输出到 IDE
Display.init(Display.ST7701, width=FRAME_W, height=FRAME_H,
             fps=90, to_ide=True)
MediaManager.init()
sensor.run()

time.sleep(2)                            # 等待自动曝光


def uart_send(raw):
    try:
        uart.write(raw)
    except Exception:
        pass


# ========================= 主循环 =========================
mode = MODE_IDLE
det_seq = 0
ack_seq = 0
hb_seq = 0
last_det = 0
last_hb = time.ticks_ms()
stable_color = COLOR_UNKNOWN
stable_count = 0

os.exitpoint(os.EXITPOINT_ENABLE)

try:
    while True:
        os.exitpoint()
        now = time.ticks_ms()

        # 1. 接收 ESP 命令并 ACK（官方例程写法：read 无数据返回 None）
        try:
            incoming = uart.read(128)
        except Exception:
            incoming = None
        if incoming:
            for msg_type, seq, payload in parse_frames(incoming):
                if msg_type == TYPE_COMMAND and len(payload) == 1 \
                        and payload[0] <= MODE_TRAY:
                    if payload[0] != mode:
                        stable_color = COLOR_UNKNOWN
                        stable_count = 0
                    mode = payload[0]
                    uart_send(build_frame(TYPE_ACK, ack_seq,
                                          bytes((seq, mode))))
                    ack_seq = (ack_seq + 1) & 0xFF

        # 2. 心跳
        if time.ticks_diff(now, last_hb) >= HEARTBEAT_MS:
            uart_send(build_frame(TYPE_HEARTBEAT, hb_seq, b""))
            hb_seq = (hb_seq + 1) & 0xFF
            last_hb = now

        # 3. 取图
        img = sensor.snapshot()
        if img is None:
            time.sleep_ms(5)
            continue

        # 准星 + 对心圈
        img.draw_circle(CENTER_X, CENTER_Y, ALIGN_RADIUS,
                        color=(255, 255, 255), thickness=1)
        img.draw_cross(CENTER_X, CENTER_Y, color=(255, 255, 255),
                       size=10, thickness=1)

        # 4. 分模式检测；IDLE 用 MATERIAL 配置做预览
        profile = PROFILES.get(mode)
        preview = False
        if profile is None and mode == MODE_IDLE:
            profile = PROFILES[MODE_MATERIAL]
            preview = True

        blobs = []
        if profile is not None:
            try:
                img.draw_rectangle(profile["roi"], color=(70, 70, 70))
            except Exception:
                pass
            blobs = detect_blobs(img, profile, True)
            if not blobs and not STRICT_ROI:
                blobs = detect_blobs(img, profile, False)

        blob = max(blobs, key=lambda b: b.pixels()) if blobs else None
        detected = blob is not None
        color = code_to_color(blob.code()) if detected else COLOR_UNKNOWN
        cx = blob.cx() if detected else 0
        cy = blob.cy() if detected else 0
        area = min(blob.area(), 65535) if detected else 0
        w = blob.w() if detected else 0
        h = blob.h() if detected else 0

        if detected:
            dx = cx - CENTER_X
            dy = cy - CENTER_Y
            aligned = math.sqrt(dx * dx + dy * dy) <= ALIGN_RADIUS
            if color == stable_color:
                stable_count += 1
            else:
                stable_color = color
                stable_count = 1
        else:
            dx = 0
            dy = 0
            aligned = False
            stable_color = COLOR_UNKNOWN
            stable_count = 0

        stable = stable_count >= STABLE_FRAMES
        confidence = 0
        if detected and w > 0 and h > 0:
            confidence = max(1, min(100, blob.pixels() * 100 // (w * h)))

        # IDLE 预览也上报（mode 仍为 0）：比赛逻辑只认 MATERIAL/RING，不受影响，
        # 但网页 vision/aim 面板在待机时也能看到物料与建议移动方向。
        report = detected

        # 5. 屏幕显示
        if detected:
            name, ui_color = COLOR_STYLES.get(color, ("UNKNOWN", (255, 255, 255)))
            img.draw_rectangle(blob.rect(), color=ui_color, thickness=2)
            img.draw_cross(cx, cy, color=ui_color, size=8, thickness=2)
            img.draw_line(CENTER_X, CENTER_Y, cx, cy,
                          color=(255, 255, 0), thickness=1)
            img.draw_string_advanced(10, 8, 24,
                                     "MODE %d%s  %s" % (mode, " PREVIEW" if preview else "", name),
                                     color=ui_color)
            img.draw_string_advanced(10, 38, 24,
                                     "dx:%d px  dy:%d px" % (dx, dy),
                                     color=(255, 255, 0))
            dir_x = "RIGHT" if dx > 0 else "LEFT"
            dir_y = "DOWN" if dy > 0 else "UP"
            img.draw_string_advanced(10, 68, 20,
                                     "%s %dpx  %s %dpx" % (dir_x, abs(dx), dir_y, abs(dy)),
                                     color=(255, 255, 0))
            img.draw_string_advanced(10, 98, 20,
                                     "ALIGNED" if aligned else "ALIGNING...",
                                     color=(0, 255, 0) if aligned else (255, 165, 0))
        else:
            img.draw_string_advanced(10, 8, 24,
                                     "MODE %d  NO TARGET (%d blobs)" % (mode, len(blobs)),
                                     color=(200, 200, 200))
            img.draw_string_advanced(10, 38, 18,
                                     "check THRESHOLDS / ROI / min_area",
                                     color=(150, 150, 150))
        img.draw_string_advanced(10, FRAME_H - 28, 18,
                                 "PORT1 UART1 115200  area:%d" % area,
                                 color=(140, 140, 140))

        # 6. 周期上报：无目标也发，保持 ESP 链路在线
        if time.ticks_diff(now, last_det) >= DETECTION_MS:
            flags = (1 if report else 0) | (2 if (stable and report) else 0)
            payload = struct.pack("<BBBHHHBB", mode,
                                  color if report else COLOR_UNKNOWN, flags,
                                  cx if report else 0, cy if report else 0,
                                  area if report else 0,
                                  confidence if report else 0, 0)
            uart_send(build_frame(TYPE_DETECTION, det_seq, payload))
            det_seq = (det_seq + 1) & 0xFF
            last_det = now

        Display.show_image(img)
        time.sleep_ms(2)

except KeyboardInterrupt as error:
    print("user stop:", error)
except BaseException as error:
    print("exception:", error)

finally:
    try:
        if isinstance(sensor, Sensor):
            sensor.stop()
    except Exception:
        pass
    try:
        Display.deinit()
    except Exception:
        pass
    try:
        os.exitpoint(os.EXITPOINT_ENABLE_SLEEP)
        time.sleep_ms(100)
    except Exception:
        pass
    try:
        MediaManager.deinit()
    except Exception:
        pass
