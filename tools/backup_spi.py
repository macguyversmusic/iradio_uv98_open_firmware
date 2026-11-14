"""Read the radio's whole 4 MB SPI flash (channels, settings, calibration) to a file.

Needs the stock firmware running (the open firmware does not answer this
protocol). Radio switched on normally, programming cable in the USB-C port.

Protocol (stock programming port, 115200 baud):
  34 52 05 10 9B      enter programming mode, reply 06
  52 hi lo sum        read 1 KB block hi:lo, reply 52 hi lo + 1024 bytes + sum
  34 52 05 EE 79      leave programming mode

usage: backup_spi.py <output .bin> [--port /dev/ttyUSB0]
"""
import argparse
import os
import struct
import sys

import serial
from serial.tools import list_ports

ENTER = bytes.fromhex("3452 0510 9B")
EXIT = bytes.fromhex("3452 05EE 79")
BLOCKS, BLOCK = 4096, 1024


def find_port():
    for p in list_ports.comports():
        if (p.vid, p.pid) == (0x0403, 0x6001):
            return p.device
    return None


def read_block(ser, n):
    cmd = struct.pack(">BH", 0x52, n)
    cmd += bytes([sum(cmd) & 0xFF])
    for _ in range(3):
        ser.reset_input_buffer()
        ser.write(cmd)
        r = ser.read(3 + BLOCK + 1)
        if len(r) == 3 + BLOCK + 1 and r[:3] == cmd[:3] and r[-1] == sum(r[:-1]) & 0xFF:
            return r[3:-1]
    sys.exit(f"block {n}: no valid reply after 3 tries")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output")
    ap.add_argument("--port")
    a = ap.parse_args()
    if os.path.exists(a.output):
        sys.exit(f"{a.output} already exists; choose another name")
    port = a.port or find_port()
    if not port:
        sys.exit("FTDI cable (0403:6001) not found; give --port")
    ser = serial.Serial(port, 115200, timeout=1)
    for _ in range(5):
        ser.write(ENTER)
        if ser.read(1) == b"\x06":
            break
    else:
        sys.exit("the radio did not accept programming mode (is the stock firmware running?)")
    data = bytearray()
    try:
        for n in range(BLOCKS):
            data += read_block(ser, n)
            if n % 256 == 0:
                print(f"{n:5d}/{BLOCKS} blocks", flush=True)
    finally:
        ser.write(EXIT)
        ser.flush()
        ser.close()
    with open(a.output, "wb") as f:
        f.write(data)
    print(f"wrote {a.output} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
