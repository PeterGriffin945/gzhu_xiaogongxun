# CanMV / K230D 视觉伺服节点（Gemini 脚本改进版）
#
# 保留原算法与写法：色块检测 -> 取最大 blob -> 画面中心准星 -> dx/dy -> 对准判定
# 新增完善：
#   1. ROI 分模式：MATERIAL 只看下方物料区，RING 只看上方靶环区
#   2. 屏幕：默认 IDE 虚拟屏（与原型一致），USE_LCD=True 时使用板载 ST7701 800x480
#   3. 与 ESP32-S3 完全兼容的 A5 5A 协议：命令 / ACK / 心跳 / 80ms 周期检测
#      CRC 只覆盖 ver..payload（不含同步字），cx/cy 为绝对坐标，flags 为
#      bit0 detected + bit1 stable，无目标也持续上报
#
# 运行前：把本文件拷到 K230D，作为 main.py 运行；UART 使用 PORT2 / UART(2)。
# 板载屏显示把 USE_LCD 改为 True。
import time
import math
import struct
from machine import UART

from media.sensor import Sensor
from media.display import Display
from media.media import MediaManager

# ========================= 可调参数 =========================
UART_PORT = 2            # ATK K230D PORT2 默认 UART(2)，不要改成 1（UART1 是调试口）
UART_BAUD = 115200
DETECTION_MS = 80        # 12.5 Hz，与 ESP 的 400ms 结果新鲜度匹配
HEARTBEAT_MS = 500

CENTER_X = 160           # QVGA 320x240 图像中心，也是 ESP 的默认目标点
CENTER_Y = 120
ALIGN_RADIUS = 15        # 仅用于屏幕提示；最终对位由 ESP 的容差与 nudge 决定
STABLE_FRAMES = 3        # 连续同色帧数达到该值才置 stable
USE_LCD = False          # False = IDE 虚拟屏（与原型一致）；True = 板载 ST7701 800x480
SENSOR_ID = None         # None = 自动探测 CSI 0/1/2；也可手动填 0、1、2

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


# ========================= 硬件初始化 =========================
# 不同 K230 底板的摄像头可能接在 CSI0/1/2 上；固件找不到时会抛
# "Can not found sensor on 2"。这里按 SENSOR_ID（默认自动）逐路探测，
# 并把每路的结果打印出来，便于确认摄像头排线/接口。
def make_sensor(sensor_id):
    if sensor_id is None:
        return Sensor()          # 与原型的写法完全一致
    try:
        return Sensor(id=sensor_id)
    except TypeError:
        # 旧固件只接受位置参数
        return Sensor(sensor_id)


def open_sensor():
    # None = 先用原型写法 Sensor()，再依次尝试 CSI 0/1/2
    candidates = (SENSOR_ID,) if SENSOR_ID is not None else (None, 0, 1, 2)
    last_error = None
    for sensor_id in candidates:
        label = "default" if sensor_id is None else ("CSI%d" % sensor_id)
        try:
            candidate = make_sensor(sensor_id)
            candidate.reset()
            candidate.set_framesize(Sensor.QVGA)      # 320x240
            candidate.set_pixformat(Sensor.RGB565)
            print("sensor %s ready" % label)
            return candidate
        except Exception as error:
            last_error = error
            print("sensor %s not found: %s" % (label, error))
    raise RuntimeError("camera not found, check ribbon/CSI port: %s" % last_error)


sensor = open_sensor()

# 屏幕：默认和原型一样用 IDE 虚拟屏；需要板载 ST7701 时把 USE_LCD 改 True
if USE_LCD:
    Display.init(Display.ST7701, width=800, height=480, to_ide=True)
else:
    Display.init(Display.VIRT, 320, 240)

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
            try:
                blobs = img.find_blobs(
                    THRESHOLDS,
                    roi=profile["roi"],
                    pixels_threshold=profile["min_pixels"],
                    area_threshold=profile["min_area"],
                    merge=True,
                )
            except Exception:
                # 个别固件不支持 roi 关键字时退回全图检测
                blobs = img.find_blobs(
                    THRESHOLDS,
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
            img.draw_string_advanced(5, 5, 20, "MODE %d  %s" % (mode, name),
                                     color=ui_color)
            img.draw_string_advanced(5, 30, 20, "dx:%d dy:%d" % (dx, dy),
                                     color=(255, 255, 0))
            img.draw_string_advanced(5, 55, 20,
                                     "ALIGNED" if aligned else "ALIGNING...",
                                     color=(0, 255, 0) if aligned else (255, 165, 0))
        else:
            img.draw_string_advanced(5, 5, 20, "MODE %d  NO TARGET" % mode,
                                     color=(200, 200, 200))
        img.draw_string_advanced(5, 80, 16,
                                 "ROI %s" % ("MATERIAL" if mode == MODE_MATERIAL else
                                             ("RING" if mode == MODE_RING else "IDLE")),
                                 color=(150, 150, 150))

        # 5. 周期上报：无目标也发 detected=0，保证 ESP 链路在线
        if time.ticks_diff(now, last_det) >= DETECTION_MS:
            flags = (1 if detected else 0) | (2 if stable else 0)
            payload = struct.pack("<BBBHHHBB", mode, color, flags,
                                  cx, cy, area, confidence, 0)
            uart_send(build_frame(TYPE_DETECTION, det_seq, payload))
            det_seq = (det_seq + 1) & 0xFF
            last_det = now

        Display.show_image(img)
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
    try:
        MediaManager.deinit()
    except Exception:
        pass
