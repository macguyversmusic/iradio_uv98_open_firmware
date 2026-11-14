"""Hamlib network (rigctld) bridge for the iRadio UV-98 Plus open firmware.

Programs that speak Hamlib's rigctld protocol (gpredict, WSJT-X, fldigi, CQRLOG, and anything using
Hamlib model 2, "NET rigctl") can tune and key the radio through this bridge, over the programming
cable. Run it, then point the program at localhost port 4532:

    .venv/bin/python tools/rigctld.py [--port 4532] [--serial /dev/ttyUSB0]

Supported: frequency, mode (USB, AM, FM, narrow FM), VFO A/B, PTT, signal strength. While PTT is on,
the bridge repeats the firmware's keep-alive every second; if the program disconnects, it unkeys.
The firmware's own limits (amateur bands, power, time-out timer) still apply.
"""
import argparse
import os
import socketserver
import sys
import threading
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tracker"))
import uv98  # noqa: E402

RIG_MODEL = 2                       # Hamlib "NET rigctl"
MODE_AM, MODE_USB, MODE_LSB, MODE_FM = 0x1, 0x4, 0x8, 0x20
MODES = MODE_AM | MODE_USB | MODE_FM
VFOS, ANT = 0x3, 0x1                # VFO A | VFO B, antenna 1
LEVEL_STRENGTH = 1 << 30
TX_RANGES = [(18_068_000, 18_168_000), (21_000_000, 21_450_000), (24_890_000, 24_990_000),
             (28_000_000, 29_700_000), (50_000_000, 54_000_000), (144_000_000, 148_000_000),
             (219_000_000, 225_000_000), (420_000_000, 450_000_000), (902_000_000, 928_000_000),
             (1_240_000_000, 1_300_000_000)]


def dump_state(extended):
    """The block Hamlib 4.5 netrigctl reads (tests/rigctl_parse.c, dump_state)."""
    out = ["1", str(RIG_MODEL), "0"]
    for lo, hi in uv98.RX_RANGES:
        out.append(f"{lo:.6f} {hi:.6f} 0x{MODES:x} -1 -1 0x{VFOS:x} 0x{ANT:x}")
    out.append("0 0 0 0 0 0 0")
    for lo, hi in TX_RANGES:
        out.append(f"{lo:.6f} {hi:.6f} 0x{MODES:x} 100 10000 0x{VFOS:x} 0x{ANT:x}")
    out.append("0 0 0 0 0 0 0")
    out += [f"0x{MODES:x} 10", "0 0"]                                     # tuning step 10 Hz
    out += [f"0x{MODE_USB:x} 2400", f"0x{MODE_AM:x} 8000", f"0x{MODE_FM:x} 12500",
            f"0x{MODE_FM:x} 25000", "0 0"]                                 # filters
    out += ["0", "0", "0", "0", "", ""]                                    # rit, xit, ifshift, announces, preamp, att
    out += ["0x0", "0x0", f"0x{LEVEL_STRENGTH:x}", "0x0", "0x0", "0x0"]
    if extended:
        out += ["vfo_ops=0x0", "ptt_type=0x1", "targetable_vfo=0x0", "has_set_vfo=1", "has_get_vfo=1",
                "has_set_freq=1", "has_get_freq=1", "has_set_conf=0", "has_get_conf=0",
                "has_power2mW=0", "has_mW2power=0", "timeout=1000", f"rig_model={RIG_MODEL}",
                "rigctld_version=uv98-open-firmware bridge", "agc_levels=", "done"]
    return "\n".join(out) + "\n"


def to_radio_mode(mode, passband):
    m = mode.upper()
    if m in ("FM", "PKTFM", "FMN"):
        return "NFM" if (m == "FMN" or (0 < passband <= 12500)) else "FM"
    if m == "AM":
        return "AM"
    return "USB"                    # USB, LSB, CW, PKTUSB, data: the radio has USB only


def from_radio_mode(mode):
    return {"NFM": ("FM", 12500), "FM": ("FM", 25000), "AM": ("AM", 8000)}.get(mode, ("USB", 2400))


LONG = {"get_freq": "f", "set_freq": "F", "get_mode": "m", "set_mode": "M", "get_vfo": "v",
        "set_vfo": "V", "get_ptt": "t", "set_ptt": "T", "get_split_vfo": "s", "set_split_vfo": "S",
        "get_level": "l", "get_powerstat": "get_powerstat", "dump_state": "dump_state",
        "chk_vfo": "chk_vfo", "quit": "q"}


class Bridge:
    def __init__(self, port):
        self.radio = uv98.UV98(port)
        self.keyed = False
        self.lock = threading.Lock()
        threading.Thread(target=self._keepalive, daemon=True).start()

    def _keepalive(self):
        while True:
            time.sleep(1.0)
            with self.lock:
                if self.keyed:
                    try:
                        self.radio.ptt(True)
                    except uv98.RadioError:
                        self.keyed = False

    def set_ptt(self, on):
        with self.lock:
            self.radio.ptt(on)
            self.keyed = on

    def command(self, words, state):
        cmd, args = words[0], words[1:]
        if cmd.startswith("\\"):
            cmd = LONG.get(cmd[1:], cmd)
        r = self.radio
        if cmd == "dump_state":
            return dump_state(state["chk_vfo"])
        if cmd == "chk_vfo":
            state["chk_vfo"] = True
            return "0\n"
        if cmd == "get_powerstat":
            return "1\n"
        if cmd == "f":
            return f"{r.get_status()['freq_hz']}\n"
        if cmd == "F":
            s = r.get_status()
            r.tune(int(float(args[0])), s["mode"])
            return "RPRT 0\n"
        if cmd == "m":
            mode, pb = from_radio_mode(r.get_status()["mode"])
            return f"{mode}\n{pb}\n"
        if cmd == "M":
            s = r.get_status()
            r.tune(s["freq_hz"], to_radio_mode(args[0], int(args[1]) if len(args) > 1 else 0))
            return "RPRT 0\n"
        if cmd == "v":
            return f"VFO{r.vfo()}\n"
        if cmd == "V":
            v = args[0].upper()
            if v in ("VFOA", "VFOB"):
                r.vfo(v[-1])
            return "RPRT 0\n"
        if cmd == "t":
            return f"{1 if self.keyed else 0}\n"
        if cmd == "T":
            self.set_ptt(args[0] not in ("0",))
            return "RPRT 0\n"
        if cmd == "s":
            return "0\nVFOA\n"
        if cmd == "S":
            return "RPRT 0\n"
        if cmd == "l" and args and args[0].upper() == "STRENGTH":
            s = r.get_status()
            s9 = -73 if s["freq_hz"] < 30_000_000 else -93
            return f"{s['rssi_dbm'] - s9}\n"
        return "RPRT -11\n"            # not implemented


class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        state = {"chk_vfo": False}
        try:
            for raw in self.rfile:
                words = raw.decode(errors="replace").strip().split()
                if not words:
                    continue
                if words[0] in ("q", "Q", "\\quit"):
                    break
                try:
                    reply = self.server.bridge.command(words, state)
                except (uv98.RadioError, ValueError, IndexError):
                    reply = "RPRT -1\n"
                self.wfile.write(reply.encode())
                self.wfile.flush()
        finally:
            if self.server.bridge.keyed:          # never leave the radio keyed after a disconnect
                try:
                    self.server.bridge.set_ptt(False)
                except uv98.RadioError:
                    pass


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


def main():
    ap = argparse.ArgumentParser(description="Hamlib rigctld bridge for the iRadio UV-98 Plus open firmware")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=4532)
    ap.add_argument("--serial", default=None, help="serial port (default: find the FTDI cable)")
    a = ap.parse_args()
    with Server((a.host, a.port), Handler) as srv:
        srv.bridge = Bridge(a.serial)
        print(f"rigctld bridge on {a.host}:{a.port}, radio on {srv.bridge.radio.port}", flush=True)
        srv.serve_forever()


if __name__ == "__main__":
    main()
