import struct
import time

import serial
from serial.tools import list_ports

USB_ID = (0x0403, 0x6001)
BAUD = 115200
ENTER = b"4R\x05\x10\x9b"
EXIT = b"4R\x05\xee\x79"
ACK = b"\x06"
BLOCK = 0x400
READ_OFFSET = 8
REGION_SETTINGS = 0x90

# Layout from CHIRP's radtel_rt880g driver (RT-880G = iRadio UV-98 Plus).
VFO_OFFSET = {"UV-A": 0x400, "UV-B": 0x430, "UV-C": 0x460}
AREA_MODE_OFFSET = {"A": 0x79, "B": 0x7E, "C": 0x83}
CURRENT_AREA_OFFSET = 0x75
AREA_INDEX = {"A": 0, "B": 1, "C": 2}
MODES = {"FM": (0, 0), "NFM": (0, 1), "AM": (1, 0), "USB": (2, 0)}
VFO_RANGE = (64000000, 620000000)


class RadioError(Exception):
    pass


def checksum(data):
    return sum(data) % 256


def find_port():
    for p in list_ports.comports():
        if (p.vid, p.pid) == USB_ID:
            return p.device
    return None


def read_cmd(block):
    cmd = struct.pack(">BH", ord("R"), block + READ_OFFSET)
    return cmd + bytes([checksum(cmd)])


def write_cmd(region, block, data):
    if len(data) != BLOCK:
        raise ValueError("block must be 1024 bytes")
    cmd = struct.pack(">BH", region, block) + bytes(data)
    return cmd + bytes([checksum(cmd)])


def patch_vfo(block0, block1, freq_hz, mode, vfo="UV-A", area="A"):
    """Return new copies of settings blocks 0 and 1 with the VFO set and the area on it."""
    if mode not in MODES:
        raise RadioError(f"mode must be one of {', '.join(MODES)}")
    if not VFO_RANGE[0] <= freq_hz <= VFO_RANGE[1]:
        raise RadioError(f"{freq_hz} Hz is outside the UV VFO range")
    b0, b1 = bytearray(block0), bytearray(block1)
    rec = VFO_OFFSET[vfo] - BLOCK
    rx_mode, narrow = MODES[mode]
    units = int(round(freq_hz / 10))
    b1[rec + 0] = rx_mode
    b1[rec + 3] = narrow
    b1[rec + 6:rec + 10] = struct.pack("<I", units)
    b1[rec + 10:rec + 14] = struct.pack("<I", units)
    b0[AREA_MODE_OFFSET[area]] = 0
    b0[CURRENT_AREA_OFFSET] = AREA_INDEX[area]
    return bytes(b0), bytes(b1)


class Radtel:
    def __init__(self, port=None):
        self.port = port or find_port()
        if not self.port:
            raise RadioError("Radtel/iRadio cable (FTDI 0403:6001) not found")
        self.ser = serial.Serial(baudrate=BAUD, timeout=0.25, write_timeout=2, rtscts=False)
        self.ser.rts = True
        self.ser.dtr = True
        self.ser.port = self.port
        self.ser.open()

    def close(self):
        self.ser.close()

    def enter(self):
        for _ in range(5):
            self.ser.write(ENTER)
            if self.ser.read(1) == ACK:
                return
        raise RadioError("the radio did not accept programming mode")

    def exit(self):
        self.ser.write(EXIT)
        self.ser.flush()

    def read_block(self, block):
        self.ser.write(read_cmd(block))
        resp = self.ser.read(3 + BLOCK + 1)
        if len(resp) != 3 + BLOCK + 1 or resp[:3] != b"R" + struct.pack(">H", block + READ_OFFSET):
            raise RadioError(f"bad reply reading block {block}")
        if resp[-1] != checksum(resp[:-1]):
            raise RadioError(f"checksum error reading block {block}")
        return resp[3:-1]

    def write_block(self, region, block, data):
        old = self.ser.timeout
        self.ser.timeout = 1
        try:
            self.ser.write(write_cmd(region, block, data))
            if self.ser.read(1) != ACK:
                raise RadioError(f"no ACK writing block {block}")
        finally:
            self.ser.timeout = old

    def set_vfo(self, freq_hz, mode, vfo="UV-A", area="A"):
        t0 = time.time()
        self.enter()
        try:
            old = [self.read_block(n) for n in range(4)]
            new = list(old)
            new[0], new[1] = patch_vfo(old[0], old[1], freq_hz, mode, vfo, area)
            written = []
            for n in range(4):
                self.write_block(REGION_SETTINGS, n, new[n])
                written.append(n)
        finally:
            self.exit()
        return {"freq_hz": int(round(freq_hz / 10)) * 10, "mode": mode, "vfo": vfo,
                "area": area, "blocks_written": written, "seconds": round(time.time() - t0, 2)}
