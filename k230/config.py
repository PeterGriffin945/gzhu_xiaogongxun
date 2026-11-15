"""K230 可调的硬件、通信和视觉参数。"""

# 正点原子 PORT2 默认映射为 UART(2)，此处不做任何 FPIOA 猜测或配置。
UART_PORT = 2
UART_BAUDRATE = 115200
UART_BITS = 8
UART_PARITY = None
UART_STOP = 1
UART_READ_CHUNK = 64

HEARTBEAT_PERIOD_MS = 500
DETECTION_PERIOD_MS = 80       # 标称 12.5 Hz，包含无目标 DETECTION。
ESP_LINK_TIMEOUT_MS = 2000
MAIN_LOOP_SLEEP_MS = 2

# 调试期可把日志文本写进协议 UART；正常运行必须关闭，
# 否则 "K230 ..." 文本会污染 ESP32 的二进制帧解析。
DEBUG_UART_LOG = False

CAMERA_WIDTH = 320
CAMERA_HEIGHT = 240

# 板载屏参数集中在适配层使用；若屏幕规格不同，仅需调整此处和 main.py 适配层。
DISPLAY_DRIVER = "ST7701"
DISPLAY_WIDTH = 800
DISPLAY_HEIGHT = 480
DISPLAY_TO_IDE = False

STABLE_FRAME_COUNT = 3

# CanMV/OpenMV LAB 阈值顺序为 (L_min, L_max, A_min, A_max, B_min, B_max)。
# 这些仅是 GC2093 的初始值，必须按现场光照、镜头和物料实测标定。
LAB_THRESHOLDS = {
    1: (10, 80, 15, 127, 0, 127),       # RED
    2: (15, 90, -128, -8, -20, 90),     # GREEN
    3: (10, 80, -20, 80, -128, -10),    # BLUE
}
COLOR_SEARCH_ORDER = (1, 2, 3)

# ROI 格式为相对整幅 QVGA 图像的 (x, y, width, height)。
# min_area 是 blob 外接框面积下限，pixels_threshold 是实际着色像素下限。
VISION_PROFILES = {
    1: {                                # MATERIAL
        "roi": (0, 35, 320, 170),
        "min_area": 180,
        "pixels_threshold": 80,
        "max_blobs": 4,
    },
    2: {                                # RING
        "roi": (20, 10, 280, 220),
        "min_area": 100,
        "pixels_threshold": 50,
        "max_blobs": 4,
    },
    # 3: TRAY 仅预留协议模式，首版不执行视觉识别。
}
