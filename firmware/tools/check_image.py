"""Check a built application image against the layout of the stock RT-880 images."""
import struct
import sys

APP_BASE, APP_SIZE = 0x08002800, 251904
RAM_LO, RAM_HI = 0x20000000, 0x2000C000

d = open(sys.argv[1], "rb").read()
sp, reset = struct.unpack_from("<II", d, 0)
checks = [
    (f"size {len(d)} == {APP_SIZE}", len(d) == APP_SIZE),
    (f"stack pointer {sp:#010x} in RAM", RAM_LO < sp <= RAM_HI),
    (f"reset vector {reset:#010x} is Thumb code inside the image", reset & 1 and APP_BASE <= reset < APP_BASE + APP_SIZE),
]
used = max(i for i in range(len(d)) if d[i] != 0) + 1
checks.append((f"code and data use {used} bytes ({used / 1024:.1f} KB)", used < APP_SIZE))
ok = True
for text, good in checks:
    print(("OK   " if good else "FAIL ") + text)
    ok &= bool(good)
sys.exit(0 if ok else 1)
