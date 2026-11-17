# CanMV/MicroPython script for ATK K230D AGV Vision
# Function: Visual Servoing - tracks circular blocks, calculates dx/dy to screen center, and triggers grab
import time, math, struct
from machine import UART
from machine import FPIOA
import image

from media.sensor import *
from media.display import *
from media.media import *

# 1. 硬件初始化
sensor = Sensor()
sensor.reset()
sensor.set_framesize(Sensor.QVGA) # 320x240
sensor.set_pixformat(Sensor.RGB565)

# 初始化 IDE 虚拟显示或实体屏幕
Display.init(Display.VIRT, 320, 240)

MediaManager.init()
sensor.run()

time.sleep(2) # 等待摄像头自动曝光稳定

# 初始化 UART
fpioa = FPIOA()
# fpioa.set_function(43, FPIOA.UART1_TXD)
# fpioa.set_function(44, FPIOA.UART1_RXD)
uart = UART(1, baudrate=115200, bits=8, parity=0, stop=1)

# 2. LAB 颜色阈值 (0:红, 1:绿, 2:蓝)
thresholds = [
    (41, 99, -28, 24, 24, 91),    # 0: 红色
    (30, 80, -60, -15, 5, 50),  # 1: 绿色
    (20, 70, -10, 30, -60, -10) # 2: 蓝色
]

def crc8(data):
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ 0x07
            else:
                crc <<= 1
        crc &= 0xFF
    return crc

# --- 视觉伺服参数 ---
CENTER_X = 160        # 屏幕 X 中心 (320/2)
CENTER_Y = 120        # 屏幕 Y 中心 (240/2)
ALIGN_RADIUS = 15     # 对准容差半径 (像素)，进入此范围视为对准完成

seq = 0
try:
    while True:
        img = sensor.snapshot()
        if img is None:
            continue

        # 绘制屏幕中心准星与容差圈
        img.draw_circle(CENTER_X, CENTER_Y, ALIGN_RADIUS, color=(255, 255, 255), thickness=1)
        img.draw_cross(CENTER_X, CENTER_Y, color=(255, 255, 255), size=8, thickness=1)

        blobs = img.find_blobs(thresholds, pixels_threshold=150, area_threshold=150, merge=True)

        if blobs:
            largest_blob = max(blobs, key=lambda b: b.pixels())

            # 提取核心数据
            cx = largest_blob.cx()
            cy = largest_blob.cy()
            area = largest_blob.area()
            color_code = largest_blob.code()

            # 计算目标中心与屏幕中心的偏差 (支持负数，用于指示方向)
            dx = cx - CENTER_X
            dy = cy - CENTER_Y

            # 计算几何距离，判断是否对准
            distance = math.sqrt(dx**2 + dy**2)
            is_aligned = 1 if distance <= ALIGN_RADIUS else 0

            # 映射颜色名称与 UI 显示颜色
            color_id = 0
            color_name = "UNKNOWN"
            ui_color = (255, 255, 255)

            if color_code == 1:
                color_id = 1; color_name = "RED"; ui_color = (255, 50, 50)
            elif color_code == 2:
                color_id = 2; color_name = "GREEN"; ui_color = (50, 255, 50)
            elif color_code == 4:
                color_id = 3; color_name = "BLUE"; ui_color = (50, 150, 255)

            # 在物料上画外接圆(针对圆形物料更直观)和十字
            radius = int(max(largest_blob.w(), largest_blob.h()) / 2)
            img.draw_circle(cx, cy, radius, color=ui_color, thickness=2)
            img.draw_cross(cx, cy, color=ui_color, size=6)

            # 画一条连线指示当前的偏差方向
            img.draw_line(CENTER_X, CENTER_Y, cx, cy, color=(255, 255, 0), thickness=1)

            # 屏幕左侧 UI 显示
            img.draw_string_advanced(5, 5, 20, "Obj: " + color_name, color=ui_color)
            img.draw_string_advanced(5, 30, 20, "dx:{} dy:{}".format(dx, dy), color=(255, 255, 0))

            # 对准状态判定与 UI 提示
            if is_aligned:
                img.draw_string_advanced(5, 55, 20, "STATUS: ALIGNED [GRAB!]", color=(0, 255, 0))
            else:
                img.draw_string_advanced(5, 55, 20, "STATUS: ALIGNING...", color=(255, 165, 0))

            # 打包二进制帧发送给 ESP32-S3
            # 【协议复用】：将 dx, dy 直接塞进原来 cx, cy 的位置 (h 代表 2字节有符号整数 int16_t)
            # 【指令复用】：将 is_aligned 塞进 flags 字节，1 代表可以抓取，0 代表继续微调
            mode = 1 # 1=MATERIAL, 2=RING
            payload = struct.pack("<BBBhhhBB", mode, color_id, is_aligned, dx, dy, area, 99, 0)
            header = struct.pack("<BBBBBB", 0xA5, 0x5A, 0x01, 0x10, seq % 256, 11)

            frame_no_crc = header + payload
            crc_val = crc8(frame_no_crc)
            frame = frame_no_crc + struct.pack("<B", crc_val)

            uart.write(frame)
            seq += 1

        Display.show_image(img)
except KeyboardInterrupt:
    pass
finally:
    MediaManager.deinit()
