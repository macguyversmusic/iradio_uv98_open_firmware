"""RT-880 / UV-98 bootloader client (same packet sequence as nicsure's RT880-FlasherX).

The radio must be in its bootloader: switched on with PTT held.

  probe  send only the connect packet and report the reply (erases nothing)
  flash  connect, erase, write 246 x 1 KB blocks (needs --yes)
"""
import argparse
import struct
import sys
import time

import serial
from serial.tools import list_ports

APP_BASE, APP_SIZE = 0x08002800, 251904
SEED = {"rt880": 0x52, "uv98": 0x00}


def packet(kind, address, data, seed):
    p = bytearray([kind & 0xFF, (address >> 8) & 0xFF, address & 0xFF]) + bytes(data)
    p.append((seed + sum(p)) & 0xFF)
    return bytes(p)


def find_port():
    for p in list_ports.comports():
        if (p.vid, p.pid) == (0x0403, 0x6001):
            return p.device
    return None


def check_image(d):
    sp, reset = struct.unpack_from("<II", d, 0)
    problems = []
    if len(d) != APP_SIZE:
        problems.append(f"size {len(d)} is not {APP_SIZE}")
    if not 0x20000000 < sp <= 0x2000C000:
        problems.append(f"stack pointer {sp:#010x} not in RAM")
    if not (reset & 1 and APP_BASE <= reset < APP_BASE + APP_SIZE):
        problems.append(f"reset vector {reset:#010x} not Thumb code in the image")
    return problems


def read_reply(s, timeout):
    s.timeout = timeout
    b = s.read(1)
    return b[0] if b else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=["probe", "flash"])
    ap.add_argument("--port", default=None)
    ap.add_argument("--model", choices=SEED, default="rt880")
    ap.add_argument("--image")
    ap.add_argument("--yes", action="store_true", help="really erase and write")
    a = ap.parse_args()

    port = a.port or find_port()
    if not port:
        sys.exit("no FTDI 0403:6001 port found")
    seed = SEED[a.model]
    connect = packet(0x39, 0x3305, [0x10], seed)

    if a.action == "flash":
        if not a.image:
            sys.exit("--image is required")
        data = open(a.image, "rb").read()
        problems = check_image(data)
        if problems:
            sys.exit("image rejected: " + "; ".join(problems))
        if not a.yes:
            sys.exit("refusing to erase without --yes")

    s = serial.Serial(port, 115200, timeout=1)
    s.reset_input_buffer()
    print(f"port {port}, model {a.model} (checksum seed {seed:#04x})")
    try:
        if a.action == "probe":
            s.write(connect)
            r = read_reply(s, 2)
            print("connect " + connect.hex(" ") + " -> " + ("no reply" if r is None else f"{r:#04x}"))
            print("bootloader accepts this model's checksum" if r == 0x06 else "not acknowledged")
            return

        time.sleep(0.3)
        s.reset_input_buffer()
        acks = 0
        for attempt in range(5):
            s.write(connect)
            r = read_reply(s, 1)
            print(f"connect attempt {attempt + 1}: {'no reply' if r is None else f'{r:#04x}'}")
            if r == 0x06:
                acks += 1
                if acks == 2:
                    break
            elif r is not None:
                sys.exit(f"connect refused ({r:#04x}); nothing erased")
        if acks < 2:
            sys.exit("bootloader did not acknowledge connect; nothing erased")
        print("connected")
        s.write(packet(0x39, 0x3305, [0x55], seed))
        r = read_reply(s, 10)
        if r != 0x06:
            sys.exit(f"erase: expected 06, got {r}")
        print("erased")
        t0 = time.time()
        for off in range(0, APP_SIZE, 1024):
            s.write(packet(0x57, off, data[off:off + 1024], seed))
            r = read_reply(s, 5)
            if r != 0x06:
                sys.exit(f"block at {off:#07x}: expected 06, got {r}")
            if off % (32 * 1024) == 0:
                print(f"  {off // 1024:3d}/246 blocks", flush=True)
        r = read_reply(s, 5)
        print(f"all 246 blocks written in {time.time() - t0:.0f} s; final reply {r}")
    finally:
        s.close()


if __name__ == "__main__":
    main()
