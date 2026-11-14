"""Control an iRadio UV-98 Plus running the open firmware over its programming cable.

Uses the firmware's serial commands (115200 baud): PING, F <MHz>, M <mode>, RSSI, VOL <0-63>.
"""
import threading
import time

import serial
from serial.tools import list_ports

USB_ID = (0x0403, 0x6001)            # FTDI FT232R programming cable
RX_RANGES = ((18_000_000, 660_000_000), (840_000_000, 1_300_000_000))
MODES = ("USB", "FM", "NFM", "AM")


class RadioError(Exception):
    pass


def find_port():
    for p in list_ports.comports():
        if (p.vid, p.pid) == USB_ID:
            return p.device
    return None


def can_tune(freq_hz):
    return any(lo <= freq_hz <= hi for lo, hi in RX_RANGES)


def pick_mode(sat_mode):
    """Receive mode for a satellite transmitter mode from SatNOGS."""
    m = (sat_mode or "").upper()
    if m.startswith("FM") or "AFSK" in m:
        return "NFM"
    if m == "AM":
        return "AM"
    return "USB"                     # SSB, CW, BPSK, FSK and other data modes


class UV98:
    def __init__(self, port=None):
        self.port = port or find_port()
        if not self.port:
            raise RadioError("programming cable (FTDI 0403:6001) not found")
        try:
            self.ser = serial.Serial(self.port, 115200, timeout=0.05)
        except serial.SerialException as e:
            raise RadioError(str(e))
        self.volume = 50
        self.lock = threading.Lock()      # one command at a time (the page polls while you click)
        if not any("PONG" in l for l in self._cmd("PING")):
            self.close()
            raise RadioError("no reply from the radio; is the open firmware running and the radio on?")

    @property
    def alive(self):
        return self.ser is not None and self.ser.is_open

    def close(self):
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None

    def _cmd(self, text, quiet=0.15, limit=1.0):
        """Send one command; return the reply lines (read until the line is quiet)."""
        with self.lock:
            return self._cmd_locked(text, quiet, limit)

    def _cmd_locked(self, text, quiet, limit):
        if not self.alive:
            raise RadioError("not connected")
        try:
            self.ser.reset_input_buffer()
            self.ser.write(text.encode() + b"\r\n")
            buf, t0, last = b"", time.time(), time.time()
            while time.time() - t0 < limit:
                chunk = self.ser.read(256)
                if chunk:
                    buf += chunk
                    last = time.time()
                elif buf and time.time() - last > quiet:
                    break
        except serial.SerialException as e:
            self.close()
            raise RadioError(str(e))
        return [l.strip() for l in buf.decode(errors="replace").splitlines() if l.strip()]

    def get_status(self):
        s = {"freq_hz": None, "mode": None, "rssi_dbm": None, "volume": self.volume}
        for line in self._cmd("F") + self._cmd("RSSI"):
            p = line.split()
            try:
                if len(p) >= 4 and p[0] == "FREQ":
                    s["freq_hz"] = int(round(float(p[1]) * 1e6))
                    s["mode"] = p[3]
                elif len(p) >= 2 and p[0] == "RSSI":
                    s["rssi_dbm"] = int(p[1])
            except ValueError:
                continue                  # a line garbled by other output; the next poll fixes it
        if s["freq_hz"] is None:
            raise RadioError("no frequency report from the radio")
        return s

    def tune(self, freq_hz, mode):
        if not can_tune(freq_hz):
            raise RadioError(f"{freq_hz / 1e6:.5f} MHz is outside the radio's receive range")
        if mode not in MODES:
            raise RadioError(f"mode must be one of {', '.join(MODES)}")
        for cmd in (f"M {mode}", f"F {freq_hz / 1e6:.5f}"):
            reply = self._cmd(cmd)
            if not any(l.startswith("FREQ") for l in reply):
                raise RadioError("; ".join(reply) or f"no reply to {cmd}")
        return self.get_status()

    def step(self, delta_hz):
        s = self.get_status()
        return self.tune(s["freq_hz"] + int(delta_hz), s["mode"])

    def ptt(self, on):
        """Key or unkey the transmitter. The firmware drops PTT unless PTT ON is repeated within 2 s."""
        reply = self._cmd("PTT ON" if on else "PTT OFF")
        if not any(l.startswith("PTT") for l in reply):
            raise RadioError("; ".join(reply) or "no reply to PTT")
        return on

    def vfo(self, name=None):
        """Return the current VFO ("A" or "B"), selecting it first if name is given."""
        reply = self._cmd(f"VFO {name}" if name else "VFO")
        for l in reply:
            if l.startswith("VFO "):
                return l.split()[1]
        raise RadioError("; ".join(reply) or "no reply to VFO")

    def set_volume(self, level):
        level = max(0, min(63, int(level)))
        reply = self._cmd(f"VOL {level}")
        if not any(l.startswith("VOL") for l in reply):
            raise RadioError("; ".join(reply) or "no reply to VOL")
        self.volume = level
        return self.get_status()
