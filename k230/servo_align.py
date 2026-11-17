# CanMV / K230D 视觉伺服节点（Gemini 脚本改进版）
#
# 保留原算法与写法：色块检测 -> 取最大 blob -> 画面中心准星 -> dx/dy -> 对准判定
# 新增完善：
#   1. ROI 分模式：MATERIAL 只看下方物料区，RING 只看上方靶环区
#   2. 屏幕自适应：优先 800x480 ST7701 实体屏（2 倍缩放居中），失败退回 IDE 虚拟屏
#   3. 与 ESP32-S3 完全兼容的 A5 5A 协议：命令 / ACK / 心跳 / 80ms 周期检测
#      CRC 只覆盖 ver..payload（不含同步字），cx/cy 为绝对坐标，flags 为
#      bit0 detected + bit1 stable，无目标也持续上报
#
# 运行前：把本文件拷到 K230D，作为 main.py 运行；UART 使用 PORT2 / UART(2)。
# 若主要用 IDE 预览，把 PREFER_IDE_DISPLAY 改为 True。
import time
import math
import struct
from machine import UART
import image

from media.sensor import *
from media.display import *
from media.media import *

# ========================= 可调参数 =========================
UART_PORT = 2            # ATK K230D PORT2 默认 UART(2)，不要改成 1（UART1 是调试口）
UART_BAUD = 115200
DETECTION_MS = 80        # 12.5 Hz，与 ESP 的 400ms 结果新鲜度匹配
HEARTBEAT_MS = 500

CENTER_X = 160           # QVGA 320x240 图像中心，也是 ESP 的默认目标点
CENTER_Y = 120
ALIGN_RADIUS = 15        # 仅用于屏幕提示；最终对位由 ESP 的容差与 nudge 决定
STABLE_FRAMES = 3        # 连续同色帧数达到该值才置 stable
PREFER_IDE_DISPLAY = False   # True = 优先 VIRT 虚拟屏；False = 优先实体 ST7701

MODE_IDLE, MODE_MATERIAL, MODE_RING, MODE_TRAY = 0, 1, 2, 3
COLOR_UNKNOWN, COLOR_RED, COLOR_GREEN, COLOR_BLUE = 0, 1, 2, 3
TYPE_COMMAND, TYPE_HEARTBEAT, TYPE_ACK, TYPE_DETECTION = 0x01, 0x02, 0x03, 0x10

# ROI 格式：(x, y, w, h)，相对 QVGA 整幅图像
# min_pixels / min_area 是色块有效像素与外接框面积下限，按现场物料大小调整
PROFILES = {
    MODE_MATERIAL: {"roi": (0, 35, 320, 170), "min_pixels": 150, "min_area": 150},
    MODE_RING:     {"roi": (20, 10, 280, 220), "min_pixels": 100, "min_area": 100},
}

# LAB 阈值 (L_min, L_max, A_min, A_max, B_min, B_max)，必须按现场光照重标
THRESHOLDS = [
    (41, 99, -28, 24, 24, 91),     # 0: 红色
    (30, 80, -60, -15, 5, 50),     # 1: 绿色
    (20, 70, -10, 30, -60, -10),   # 2: 蓝色
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
    # 与 ESP 的 vision_protocol.c 一致：CRC 覆盖 ver/type/seq/len/payload，
    # 不包含 A5 5A 同步字。
    body = bytes((0x01, msg_type, seq & 0xFF, len(payload))) + payload
    return b"\xA5\x5A" + body + bytes((crc8(body),))


_rx = b""


def parse_frames(data):
    """增量解析，CRC/版本/长度错误时只丢一个字节重新同步。"""
    global _rx
    frames = []
    if data:
        _rx += data
    while True:
        index = _rx.find(b"\xA5\x5A")
        if index < 0:
            # 末尾单独的 A5 可能是下一帧同步字的前半段
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
    # find_blobs 的 code 是阈值位掩码：红=1、绿=2、蓝=4（merge 后可能组合）
    if code & 0x01:
        return COLOR_RED
    if code & 0x02:
        return COLOR_GREEN
    if code & 0x04:
        return COLOR_BLUE
    return COLOR_UNKNOWN


# ========================= 屏幕适配层 =========================
class DisplayAdapter:
    """优先实体屏 ST7701 800x480（QVGA 2 倍缩放居中），失败退回 IDE 虚拟屏。"""

    def __init__(self):
        self.mode = "none"
        self.canvas = None
        self.panel_w = 0
        self.panel_h = 0

    def _start_lcd(self):
        driver = getattr(Display, "ST7701", None)
        if driver is None:
            return False
        # 实体屏优先；to_ide=True 兼容 IDE 预览，失败再试纯本地显示
        for to_ide in (True, False):
            try:
                Display.init(driver, width=800, height=480, to_ide=to_ide)
                self.canvas = image.Image(800, 480, image.RGB565)
                self.panel_w, self.panel_h = 800, 480
                self.mode = "lcd"
                return True
            except Exception as error:
                print("ST7701 init failed:", error)
        return False

    def start(self):
        if PREFER_IDE_DISPLAY:
            try:
                Display.init(Display.VIRT, 320, 240)
                self.mode = "virt"
                return True
            except Exception as error:
                print("VIRT display init failed:", error)
        if self._start_lcd():
            return True
        try:
            Display.init(Display.VIRT, 320, 240)
            self.mode = "virt"
            return True
        except Exception as error:
            print("VIRT display init failed:", error)
            self.mode = "none"
            return False

    def show(self, img):
        try:
            if self.mode == "virt":
                Display.show_image(img)
                return
            if self.mode != "lcd" or self.canvas is None:
                return
            # 黑边清屏 + 640x480 居中显示（2 倍放大）
            self.canvas.draw_rectangle(0, 0, self.panel_w, self.panel_h,
                                       color=(0, 0, 0), fill=True)
            try:
                self.canvas.draw_image(img, 80, 0, x_scale=2, y_scale=2)
            except Exception:
                # 旧固件不支持缩放时退化为 1:1 居中
                self.canvas.draw_image(img, (self.panel_w - 320) // 2,
                                       (self.panel_h - 240) // 2)
            Display.show_image(self.canvas)
        except Exception:
            # 屏幕异常不影响视觉与串口主流程
            pass


def draw_text(img, x, y, size, text, color=(255, 255, 255)):
    """兼容不同固件字库：优先高级字体，失败退回 draw_string。"""
    try:
        img.draw_string_advanced(x, y, size, text, color=color)
        return
    except Exception:
        pass
    try:
        img.draw_string(x, y, text, color=color, scale=max(1, size // 12))
    except Exception:
        img.draw_string(x, y, text, color=color)


# ========================= 硬件初始化 =========================
sensor = Sensor()
sensor.reset()
try:
    sensor.set_framesize(Sensor.QVGA)
except Exception:
    sensor.set_framesize(width=320, height=240)
sensor.set_pixformat(Sensor.RGB565)

display = DisplayAdapter()
display.start()

MediaManager.init()
sensor.run()
time.sleep(2)         # 等待自动曝光稳定

uart = None
try:
    # 不设置 FPIOA：ATK K230D 的 PORT2 默认映射为 UART(2)
    uart = UART(UART_PORT, baudrate=UART_BAUD, bits=8, parity=None, stop=1)
except Exception as error:
    print("UART init failed:", error)


def uart_send(raw):
    if uart is None:
        return
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

try:
    while True:
        now = time.ticks_ms()

        # 1. 接收 ESP 命令（IDLE / MATERIAL / RING / TRAY）并立即 ACK
        if uart is not None:
            try:
                incoming = uart.read() if uart.any() else None
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

        # 2. 心跳，维持 ESP 侧在线判定
        if time.ticks_diff(now, last_hb) >= HEARTBEAT_MS:
            uart_send(build_frame(TYPE_HEARTBEAT, hb_seq, b""))
            hb_seq = (hb_seq + 1) & 0xFF
            last_hb = now

        # 3. 取图与检测
        img = sensor.snapshot()
        if img is None:
            time.sleep_ms(5)
            continue

        img.draw_circle(CENTER_X, CENTER_Y, ALIGN_RADIUS,
                        color=(255, 255, 255), thickness=1)
        img.draw_cross(CENTER_X, CENTER_Y, color=(255, 255, 255),
                       size=8, thickness=1)

        profile = PROFILES.get(mode)
        blobs = []
        if profile is not None:
            blobs = img.find_blobs(
                THRESHOLDS,
                roi=profile["roi"],
                pixels_threshold=profile["min_pixels"],
                area_threshold=profile["min_area"],
                merge=True,
            )

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

        # 4. 屏幕提示（保留原脚本的准星 / 连线 / 状态文本）
        if detected:
            name, ui_color = COLOR_STYLES.get(color, ("UNKNOWN", (255, 255, 255)))
            img.draw_circle(cx, cy, int(max(w, h) / 2),
                            color=ui_color, thickness=2)
            img.draw_cross(cx, cy, color=ui_color, size=6)
            img.draw_line(CENTER_X, CENTER_Y, cx, cy,
                          color=(255, 255, 0), thickness=1)
            draw_text(img, 5, 5, 20, "MODE %d  %s" % (mode, name), ui_color)
            draw_text(img, 5, 30, 20, "dx:%d dy:%d" % (dx, dy), (255, 255, 0))
            draw_text(img, 5, 55, 20,
                      "ALIGNED" if aligned else "ALIGNING...",
                      (0, 255, 0) if aligned else (255, 165, 0))
        else:
            draw_text(img, 5, 5, 20, "MODE %d  NO TARGET" % mode,
                      (200, 200, 200))
        draw_text(img, 5, 80, 16,
                  "ROI %s" % ("MATERIAL" if mode == MODE_MATERIAL else
                              ("RING" if mode == MODE_RING else "IDLE")),
                  (150, 150, 150))

        # 5. 周期上报：无目标也发 detected=0，保证 ESP 链路在线
        if time.ticks_diff(now, last_det) >= DETECTION_MS:
            flags = (1 if detected else 0) | (2 if stable else 0)
            payload = struct.pack("<BBBHHHBB", mode, color, flags,
                                  cx, cy, area, confidence, 0)
            uart_send(build_frame(TYPE_DETECTION, det_seq, payload))
            det_seq = (det_seq + 1) & 0xFF
            last_det = now

        display.show(img)
        time.sleep_ms(5)

except KeyboardInterrupt:
    pass
finally:
    try:
        sensor.stop()
    except Exception:
        pass
    try:
        Display.deinit()
    except Exception:
        pass
    MediaManager.deinit()
