"""Summarise the hardware a firmware image uses, from the binary and its Ghidra decompilation.

usage: hw_usage.py <image.bin> <decompiled.c>
"""
import re
import struct
import sys

BASE = 0x08002800
PORTS = {0x40020000: "A", 0x40020400: "B", 0x40020800: "C", 0x40020C00: "D", 0x40021400: "F"}
RT890_BK4819_PAIRS = {(0x71, 0x25F3), (0x72, 0x41DC), (0x71, 0x1C1C), (0x72, 0x30C2), (0x59, 0x0028), (0x37, 0x1D0F)}

fw = open(sys.argv[1], "rb").read()
src = open(sys.argv[2]).read()
parts = re.split(r"\n// (FUN_[0-9a-f]+) @ ([0-9a-f]+)\n", src)
bodies = {parts[k]: parts[k + 2] for k in range(1, len(parts) - 2, 3)}


def word(addr):
    o = addr - BASE
    return struct.unpack_from("<I", fw, o)[0] if 0 <= o <= len(fw) - 4 else None


def dat(name):
    return word(int(name, 16))


def pin_name(port_base, mask):
    if mask == 0 or mask & (mask - 1):
        return f"P{PORTS[port_base]}{mask:#x}"
    return f"P{PORTS[port_base]}{mask.bit_length() - 1}"


def gpio_writes(body):
    """Constant writes to GPIO set (0x18) / clear (0x28) registers in one function body."""
    out = []
    for m in re.finditer(r"(\*?)\(?(?:undefined4 \*)?\)?\(?DAT_([0-9a-f]{8})(?:\s*\+\s*(0x[0-9a-f]+))?\)?(\[(-?\d+)\])?\s*=\s*(0x[0-9a-f]+|\d+);", body):
        v = dat(m.group(2))
        if v is None:
            continue
        addr = v + (int(m.group(3), 16) if m.group(3) else 0) + (int(m.group(5)) * 4 if m.group(5) else 0)
        port, reg = addr & ~0x3FF, addr & 0x3FF
        if port in PORTS and reg in (0x18, 0x28):
            out.append((pin_name(port, int(m.group(6), 0)), "set" if reg == 0x18 else "clr"))
    return out


def find_func(pattern):
    return [n for n, b in bodies.items() if re.search(pattern, b)]


print("== interrupt vectors in use (IRQ number: handler)")
vec = struct.unpack_from("<120I", fw, 0)
print("  ", {i - 16: hex(v) for i, v in enumerate(vec) if i >= 16 and v & 1 and BASE <= v < BASE + len(fw) and v != vec[16]})

mux_fn = [n for n in find_func(r"\(param_1 \+ 0x24\)") if re.search(r"\(param_1 \+ 0x20\)", bodies[n])]
read_fn = [n for n in find_func(r"\(param_1 \+ 0x10\) & param_2") if len(bodies[n].splitlines()) < 20]
print("== pin-mux function", mux_fn, "| pin-read function", read_fn)
mux = set()
for n, b in bodies.items():
    for f in mux_fn:
        for m in re.finditer(f + r"\(DAT_([0-9a-f]{8}),\s*(0x[0-9a-f]+|\d+),\s*(0x[0-9a-f]+|\d+)\)", b):
            p = dat(m.group(1))
            if p in PORTS:
                mux.add((f"P{PORTS[p]}{int(m.group(2), 0)}", int(m.group(3), 0)))
print("   mux:", sorted(mux))
reads = set()
for n, b in bodies.items():
    for f in read_fn:
        for m in re.finditer(f + r"\(DAT_([0-9a-f]{8}),\s*(0x[0-9a-f]+|\d+)\)", b):
            p = dat(m.group(1))
            if p in PORTS:
                reads.add(pin_name(p, int(m.group(2), 0)))
print("   input reads:", sorted(reads))

print("== BK48x9 write functions (called with RT-890 register/value pairs)")
hits = {}
for n, b in bodies.items():
    for m in re.finditer(r"(FUN_[0-9a-f]+)\((0x[0-9a-f]+),\s*(0x[0-9a-f]+)\)", b):
        if (int(m.group(2), 16), int(m.group(3), 16)) in RT890_BK4819_PAIRS:
            hits[m.group(1)] = hits.get(m.group(1), 0) + 1
for f, c in sorted(hits.items(), key=lambda kv: -kv[1]):
    helpers = re.findall(r"(FUN_[0-9a-f]+)\(", bodies.get(f, ""))
    pins = gpio_writes(bodies.get(f, ""))
    for h in set(helpers):
        pins += gpio_writes(bodies.get(h, ""))
    print(f"   {f} x{c}: pins {sorted(set(pins))}")

print("== transmit enable (reg 0x30 = 0xC1FE / 0xC3FA) and amplifier pins")
tx = find_func(r"\(0x30,\s*0xc(1fe|3fa)\)")
print("   TX-enable functions:", tx)
for n, b in bodies.items():
    w = gpio_writes(b)
    if any(p in ("PC15", "PB11") for p, _ in w):
        print(f"   {n}: {sorted(set(w))}")

print("== display")
spi3 = [n for n, b in bodies.items() if any(dat(d) == 0x40003C00 for d in set(re.findall(r"DAT_([0-9a-f]{8})", b)))]
print("   SPI3 users:", spi3)
init = [n for n, b in bodies.items() if re.search(r"\(0x36\);", b) and re.search(r"\(0xa8\);", b) and re.search(r"\(0x3a\);", b)]
for n in init:
    seq = re.findall(r"FUN_[0-9a-f]+\((0x[0-9a-f]+|\d+)\);", bodies[n])
    print(f"   init {n}: first bytes {seq[:24]}")
    for h in set(re.findall(r"(FUN_[0-9a-f]+)\(", bodies[n])):
        w = gpio_writes(bodies.get(h, ""))
        if w:
            print(f"      helper {h}: {sorted(set(w))}")
    print(f"      in init: {sorted(set(gpio_writes(bodies[n])))}")

print("== DAC (0x40007410) writers")
for n, b in bodies.items():
    if any(dat(d) == 0x40007410 for d in set(re.findall(r"DAT_([0-9a-f]{8})", b))):
        tables = [hex(dat(d)) for d in set(re.findall(r"DAT_([0-9a-f]{8})", b)) if dat(d) not in (0x40007410, None)]
        print(f"   {n}: other constants {tables}")

print("== USART6 / USART2 users")
for name, base in (("USART6", 0x40011400), ("USART2", 0x40004400), ("USART1", 0x40011000), ("USART3", 0x40004800)):
    users = [n for n, b in bodies.items() if any(dat(d) == base for d in set(re.findall(r"DAT_([0-9a-f]{8})", b)))]
    print(f"   {name}: {len(users)} functions")
