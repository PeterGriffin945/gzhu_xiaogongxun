"""ESP32-S3 与 K230 的串口帧协议；本文件不依赖任何硬件模块。"""

SYNC_MAGIC = b"\xA5\x5A"
PROTOCOL_VERSION = 0x01
MAX_PAYLOAD_LENGTH = 32
FRAME_OVERHEAD = 7
MAX_FRAME_LENGTH = FRAME_OVERHEAD + MAX_PAYLOAD_LENGTH

TYPE_COMMAND = 0x01
TYPE_HEARTBEAT = 0x02
TYPE_ACK = 0x03
TYPE_DETECTION = 0x10

MODE_IDLE = 0
MODE_MATERIAL = 1
MODE_RING = 2
MODE_TRAY = 3
VALID_MODES = (MODE_IDLE, MODE_MATERIAL, MODE_RING, MODE_TRAY)

COLOR_UNKNOWN = 0
COLOR_RED = 1
COLOR_GREEN = 2
COLOR_BLUE = 3


def crc8(data):
    """CRC-8：多项式 0x07、初值 0、无反射、xorout 0。"""
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def _check_byte(value, name):
    value = int(value)
    if value < 0 or value > 0xFF:
        raise ValueError(name + " must be in 0..255")
    return value


def _clamp(value, minimum, maximum):
    return max(minimum, min(maximum, int(value)))


class ParsedFrame:
    __slots__ = ("frame_type", "seq", "payload")

    def __init__(self, frame_type, seq, payload):
        self.frame_type = frame_type
        self.seq = seq
        self.payload = payload


def encode_frame(frame_type, seq, payload=b"", version=PROTOCOL_VERSION):
    """生成完整帧，CRC 覆盖 version 至 payload 末尾。"""
    frame_type = _check_byte(frame_type, "frame_type")
    seq = _check_byte(seq, "seq")
    version = _check_byte(version, "version")
    payload = bytes(payload)
    if len(payload) > MAX_PAYLOAD_LENGTH:
        raise ValueError("payload is longer than 32 bytes")

    body = bytes((version, frame_type, seq, len(payload))) + payload
    return SYNC_MAGIC + body + bytes((crc8(body),))


def decode_frame(raw_frame):
    """校验并解码一个完整帧；截断、粘包数据应交给 FrameParser。"""
    raw_frame = bytes(raw_frame)
    if len(raw_frame) < FRAME_OVERHEAD:
        raise ValueError("frame is too short")
    if raw_frame[:2] != SYNC_MAGIC:
        raise ValueError("bad sync magic")
    if raw_frame[2] != PROTOCOL_VERSION:
        raise ValueError("unsupported protocol version")

    payload_length = raw_frame[5]
    if payload_length > MAX_PAYLOAD_LENGTH:
        raise ValueError("payload is longer than 32 bytes")
    expected_length = FRAME_OVERHEAD + payload_length
    if len(raw_frame) != expected_length:
        raise ValueError("frame length does not match payload length")
    if crc8(raw_frame[2:-1]) != raw_frame[-1]:
        raise ValueError("CRC mismatch")

    payload_start = 6
    return ParsedFrame(
        raw_frame[3],
        raw_frame[4],
        raw_frame[payload_start:payload_start + payload_length],
    )


class FrameParser:
    """流式解析器：逐段找同步字，失败仅丢弃一个字节后重新同步。"""

    def __init__(self):
        self._buffer = bytearray()

    def reset(self):
        del self._buffer[:]

    def feed(self, data):
        """输入任意字节流，返回其中的全部完整合法帧。"""
        data = bytes(data)
        frames = []
        # 分块后及时解析，避免一次粘入大量数据时缓冲区无限增长。
        for offset in range(0, len(data), MAX_FRAME_LENGTH):
            self._buffer.extend(data[offset:offset + MAX_FRAME_LENGTH])
            self._parse_available(frames)
        return frames

    def _parse_available(self, frames):
        while True:
            sync_index = self._buffer.find(SYNC_MAGIC)
            if sync_index < 0:
                # 末尾的 A5 可能是下一帧同步字的前半段。
                if self._buffer and self._buffer[-1] == SYNC_MAGIC[0]:
                    del self._buffer[:-1]
                else:
                    del self._buffer[:]
                return

            if sync_index > 0:
                del self._buffer[:sync_index]
            if len(self._buffer) < 6:
                return

            payload_length = self._buffer[5]
            if (self._buffer[2] != PROTOCOL_VERSION or
                    payload_length > MAX_PAYLOAD_LENGTH):
                del self._buffer[0]
                continue

            frame_length = FRAME_OVERHEAD + payload_length
            if len(self._buffer) < frame_length:
                return

            if crc8(self._buffer[2:frame_length - 1]) != self._buffer[frame_length - 1]:
                del self._buffer[0]
                continue

            raw_frame = bytes(self._buffer[:frame_length])
            del self._buffer[:frame_length]
            frames.append(decode_frame(raw_frame))


class TxSequence:
    """K230 独立发送序号，所有发送帧共用并按 0..255 循环。"""

    def __init__(self, initial=0):
        self._next = _check_byte(initial, "initial")

    def next(self):
        current = self._next
        self._next = (self._next + 1) & 0xFF
        return current


def build_heartbeat(seq):
    return encode_frame(TYPE_HEARTBEAT, seq)


def build_ack(seq, command_seq, current_mode):
    payload = bytes((
        _check_byte(command_seq, "command_seq"),
        _check_byte(current_mode, "current_mode"),
    ))
    return encode_frame(TYPE_ACK, seq, payload)


def build_detection(seq, mode, color=COLOR_UNKNOWN, detected=False,
                    stable=False, cx=0, cy=0, area=0, confidence=0):
    """构造 11 字节 DETECTION；无目标时颜色、坐标、面积和置信度均清零。"""
    detected = bool(detected)
    stable = bool(stable) and detected
    if not detected:
        color = COLOR_UNKNOWN
        cx = 0
        cy = 0
        area = 0
        confidence = 0

    flags = (0x01 if detected else 0x00) | (0x02 if stable else 0x00)
    cx = _clamp(cx, 0, 0xFFFF)
    cy = _clamp(cy, 0, 0xFFFF)
    # 协议面积只有 16 位，超大色块饱和上报而不回绕成小面积。
    area = _clamp(area, 0, 0xFFFF)
    confidence = _clamp(confidence, 0, 100)
    payload = bytes((
        _check_byte(mode, "mode"),
        _check_byte(color, "color"),
        flags,
        cx & 0xFF,
        (cx >> 8) & 0xFF,
        cy & 0xFF,
        (cy >> 8) & 0xFF,
        area & 0xFF,
        (area >> 8) & 0xFF,
        confidence,
        0,
    ))
    return encode_frame(TYPE_DETECTION, seq, payload)
