"""正点原子 K230D / CanMV 主程序；硬件 API 差异集中在本文件处理。"""

import sys
import time
from machine import UART

from config import (
    CAMERA_HEIGHT,
    CAMERA_WIDTH,
    DEBUG_UART_LOG,
    DETECTION_PERIOD_MS,
    DISPLAY_DRIVER,
    DISPLAY_HEIGHT,
    DISPLAY_TO_IDE,
    DISPLAY_WIDTH,
    ESP_LINK_TIMEOUT_MS,
    HEARTBEAT_PERIOD_MS,
    MAIN_LOOP_SLEEP_MS,
    UART_BAUDRATE,
    UART_BITS,
    UART_PARITY,
    UART_PORT,
    UART_READ_CHUNK,
    UART_STOP,
    VISION_PROFILES,
)
from protocol import (
    COLOR_BLUE,
    COLOR_GREEN,
    COLOR_RED,
    MODE_IDLE,
    MODE_TRAY,
    TYPE_COMMAND,
    TYPE_HEARTBEAT,
    VALID_MODES,
    FrameParser,
    TxSequence,
    build_ack,
    build_detection,
    build_heartbeat,
)
from vision import VisionProcessor, empty_detection


COLOR_STYLES = {
    COLOR_RED: ("红色 RED", "RED", (210, 24, 24)),
    COLOR_GREEN: ("绿色 GREEN", "GREEN", (0, 150, 55)),
    COLOR_BLUE: ("蓝色 BLUE", "BLUE", (25, 70, 210)),
}
BACKGROUND_WAIT = (30, 70, 95)
BACKGROUND_UNKNOWN = (75, 75, 75)
BACKGROUND_COMM_ERROR = (190, 85, 0)
BACKGROUND_ERROR = (150, 20, 110)
TEXT_WHITE = (255, 255, 255)


class CommunicationError(Exception):
    pass


def safe_uart_log(uart, message):
    # 协议 UART 只能承载二进制帧；调试文本默认丢弃，避免 ESP32 解析器误锁帧头。
    if uart is None or not DEBUG_UART_LOG:
        return
    try:
        uart.write(("K230 " + message + "\r\n").encode("utf-8"))
    except Exception:
        pass


def report_exception(uart, error):
    try:
        sys.print_exception(error)
    except Exception:
        print(error)
    safe_uart_log(uart, "ERROR: " + str(error))


def uart_read(uart):
    try:
        available = uart.any()
        if not available:
            return None
        return uart.read(min(available, UART_READ_CHUNK))
    except Exception as error:
        raise CommunicationError("UART2 read failed: " + str(error))


def uart_send(uart, raw_frame):
    try:
        written = uart.write(raw_frame)
        if written is not None and written != len(raw_frame):
            raise CommunicationError("UART2 short write")
    except CommunicationError:
        raise
    except Exception as error:
        raise CommunicationError("UART2 write failed: " + str(error))


class CanMVHardware:
    """仅使用常见 media.camera/media.display/media.media 接口。"""

    def __init__(self, logger=None):
        self.logger = logger
        self.camera = None
        self.status_image = None
        self.display_ready = False
        self.media_ready = False
        self.camera_running = False
        self.font_fallback = False
        self._display = None
        self._media_manager = None
        self._image_module = None

    def _log(self, message):
        if self.logger is not None:
            self.logger(message)

    def start(self):
        # 导入也放在适配层，固件缺少接口时能由主流程统一报告。
        from media.camera import Camera
        from media.display import Display
        from media.media import MediaManager
        import image

        self._display = Display
        self._media_manager = MediaManager
        self._image_module = image

        self.camera = Camera()
        self.camera.reset()
        self.camera.set_framesize(width=CAMERA_WIDTH, height=CAMERA_HEIGHT)
        self.camera.set_pixformat(Camera.RGB565)

        display_driver = getattr(Display, DISPLAY_DRIVER)
        Display.init(
            display_driver,
            width=DISPLAY_WIDTH,
            height=DISPLAY_HEIGHT,
            to_ide=DISPLAY_TO_IDE,
        )
        self.display_ready = True
        MediaManager.init()
        self.media_ready = True
        self.camera.run()
        self.camera_running = True

        self.status_image = image.Image(
            DISPLAY_WIDTH,
            DISPLAY_HEIGHT,
            image.RGB565,
        )

    def snapshot(self):
        return self.camera.snapshot()

    def _draw_text(self, canvas, x, y, size, text, english_text):
        if not self.font_fallback:
            try:
                # 固件字库支持时同时显示中英文；不支持时保留英文和色块。
                canvas.draw_string_advanced(x, y, size, text, color=TEXT_WHITE)
                return
            except Exception as error:
                self.font_fallback = True
                self._log("DISPLAY advanced font failed, use English fallback: " + str(error))

        scale = max(1, size // 12)
        try:
            canvas.draw_string(
                x, y, english_text,
                color=TEXT_WHITE,
                scale=scale,
            )
        except TypeError:
            # 某些旧固件 draw_string 不支持 scale 关键字。
            canvas.draw_string(x, y, english_text, color=TEXT_WHITE)

    def show_status(self, title, subtitle, detail, english_title,
                    english_subtitle, background):
        if self.status_image is None:
            raise RuntimeError("display status image is not initialized")

        canvas = self.status_image
        canvas.draw_rectangle(
            0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT,
            color=background,
            fill=True,
        )
        self._draw_text(canvas, 30, 65, 52, title, english_title)
        self._draw_text(canvas, 30, 185, 36, subtitle, english_subtitle)
        self._draw_text(canvas, 30, 285, 24, detail, detail)
        self._display.show_image(canvas)

    def close(self):
        if self.camera is not None and self.camera_running:
            try:
                self.camera.stop()
            except Exception as error:
                self._log("camera stop failed: " + str(error))
            self.camera_running = False
        if self.display_ready:
            try:
                self._display.deinit()
            except Exception as error:
                self._log("display deinit failed: " + str(error))
            self.display_ready = False
        if self.media_ready:
            try:
                self._media_manager.deinit()
            except Exception as error:
                self._log("media deinit failed: " + str(error))
            self.media_ready = False


def update_status(hardware, mode, result, received_link, link_lost,
                  saw_vision_frame):
    if link_lost:
        hardware.show_status(
            "通信异常 COMM LOST",
            "检查ESP32和PORT2 CHECK LINK",
            "UART2 115200 8N1",
            "COMM LOST",
            "CHECK ESP32 / PORT2",
            BACKGROUND_COMM_ERROR,
        )
        return
    if not received_link:
        hardware.show_status(
            "等待ESP WAIT ESP",
            "等待心跳或命令 WAIT LINK",
            "UART2 115200 8N1",
            "WAIT ESP",
            "WAIT HEARTBEAT / COMMAND",
            BACKGROUND_WAIT,
        )
        return
    if mode == MODE_IDLE:
        hardware.show_status(
            "待机 IDLE",
            "等待识别命令 WAIT COMMAND",
            "MATERIAL=1 RING=2 TRAY=3",
            "IDLE",
            "WAIT COMMAND",
            BACKGROUND_WAIT,
        )
        return
    if mode == MODE_TRAY:
        hardware.show_status(
            "料盘预留 TRAY RESERVED",
            "首版不识别 NOT ACTIVE",
            "MODE=3",
            "TRAY RESERVED",
            "NOT ACTIVE IN THIS VERSION",
            BACKGROUND_UNKNOWN,
        )
        return
    if not saw_vision_frame:
        hardware.show_status(
            "等待目标 WAITING",
            "正在采集图像 CAPTURING",
            "MODE=" + str(mode),
            "WAITING",
            "CAPTURING",
            BACKGROUND_WAIT,
        )
        return
    if not result.detected:
        hardware.show_status(
            "未知目标 UNKNOWN",
            "等待有效红绿蓝目标 WAITING",
            "MODE=" + str(mode) + " DETECTED=0",
            "UNKNOWN TARGET",
            "WAITING FOR RED / GREEN / BLUE",
            BACKGROUND_UNKNOWN,
        )
        return

    title, english_title, background = COLOR_STYLES.get(
        result.color,
        ("未知 UNKNOWN", "UNKNOWN", BACKGROUND_UNKNOWN),
    )
    if result.stable:
        subtitle = "稳定 STABLE"
        english_subtitle = "STABLE"
    else:
        subtitle = "检测中 CHECKING"
        english_subtitle = "CHECKING"
    detail = "CX=%d CY=%d AREA=%d CONF=%d%%" % (
        result.cx,
        result.cy,
        result.area,
        result.confidence,
    )
    hardware.show_status(
        title,
        subtitle,
        detail,
        english_title,
        english_subtitle,
        background,
    )


def run():
    uart = None
    hardware = None
    try:
        # 不设置 tx/rx 引脚，使用开发板已确认的 UART(2) / PORT2 默认映射。
        uart = UART(
            UART_PORT,
            baudrate=UART_BAUDRATE,
            bits=UART_BITS,
            parity=UART_PARITY,
            stop=UART_STOP,
        )
        hardware = CanMVHardware(lambda message: safe_uart_log(uart, message))
        hardware.start()

        parser = FrameParser()
        tx_sequence = TxSequence()
        vision = VisionProcessor()
        current_mode = MODE_IDLE
        latest_result = empty_detection(current_mode)
        received_link = False
        saw_vision_frame = False

        now = time.ticks_ms()
        last_rx_ms = now
        last_heartbeat_ms = time.ticks_add(now, -HEARTBEAT_PERIOD_MS)
        last_detection_ms = time.ticks_add(now, -DETECTION_PERIOD_MS)
        update_status(
            hardware, current_mode, latest_result,
            False, False, False,
        )

        while True:
            incoming = uart_read(uart)
            if incoming:
                for frame in parser.feed(incoming):
                    if frame.frame_type == TYPE_HEARTBEAT:
                        if len(frame.payload) == 0:
                            received_link = True
                            last_rx_ms = time.ticks_ms()
                        continue

                    if frame.frame_type != TYPE_COMMAND:
                        continue
                    if len(frame.payload) != 1:
                        continue
                    requested_mode = frame.payload[0]
                    if requested_mode not in VALID_MODES:
                        continue

                    # 每次合法 COMMAND（包括重复模式）都清投票并立即 ACK。
                    current_mode = requested_mode
                    vision.reset()
                    latest_result = empty_detection(current_mode)
                    saw_vision_frame = False
                    received_link = True
                    last_rx_ms = time.ticks_ms()
                    ack = build_ack(
                        tx_sequence.next(),
                        frame.seq,
                        current_mode,
                    )
                    uart_send(uart, ack)

            if current_mode in VISION_PROFILES:
                source_image = hardware.snapshot()
                latest_result = vision.process(source_image, current_mode)
                saw_vision_frame = True

            now = time.ticks_ms()
            if time.ticks_diff(now, last_heartbeat_ms) >= HEARTBEAT_PERIOD_MS:
                uart_send(uart, build_heartbeat(tx_sequence.next()))
                last_heartbeat_ms = now

            if time.ticks_diff(now, last_detection_ms) >= DETECTION_PERIOD_MS:
                detection = build_detection(
                    tx_sequence.next(),
                    latest_result.mode,
                    color=latest_result.color,
                    detected=latest_result.detected,
                    stable=latest_result.stable,
                    cx=latest_result.cx,
                    cy=latest_result.cy,
                    area=latest_result.area,
                    confidence=latest_result.confidence,
                )
                uart_send(uart, detection)
                last_detection_ms = now

                link_lost = (
                    time.ticks_diff(now, last_rx_ms) > ESP_LINK_TIMEOUT_MS
                )
                update_status(
                    hardware,
                    current_mode,
                    latest_result,
                    received_link,
                    link_lost,
                    saw_vision_frame,
                )

            time.sleep_ms(MAIN_LOOP_SLEEP_MS)

    except KeyboardInterrupt:
        if hardware is not None:
            hardware.close()
    except Exception as error:
        report_exception(uart, error)
        if hardware is not None:
            try:
                if isinstance(error, CommunicationError):
                    hardware.show_status(
                        "通信异常 COMM ERROR",
                        "串口读写失败 UART FAILURE",
                        str(error)[:64],
                        "COMM ERROR",
                        "UART READ / WRITE FAILURE",
                        BACKGROUND_COMM_ERROR,
                    )
                else:
                    hardware.show_status(
                        "系统错误 SYSTEM ERROR",
                        "检查串口错误信息 CHECK UART LOG",
                        str(error)[:64],
                        "SYSTEM ERROR",
                        "CHECK UART ERROR LOG",
                        BACKGROUND_ERROR,
                    )
            except Exception as display_error:
                report_exception(uart, display_error)

        # 保留错误画面，避免自动重启后反复抓取或隐藏故障；排除故障后复位。
        while True:
            time.sleep_ms(1000)


run()
