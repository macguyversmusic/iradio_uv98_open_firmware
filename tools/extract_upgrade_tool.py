"""Extract the firmware image from iRadio's UV-98 Plus upgrade tool.

The upgrade tool (.NET .exe) stores the firmware as Intel HEX text in UTF-16.
This finds it, checks every record checksum, and writes a 251,904-byte
application image (0x08002800-0x0803FFFF, unused bytes 0x00) that flash.py
can write.

usage: extract_upgrade_tool.py <upgrade tool .exe or .zip> <output .bin>
"""
import re
import sys
import zipfile

APP_BASE, APP_END = 0x08002800, 0x08040000


def read_exe(path):
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            names = [n for n in z.namelist() if n.lower().endswith(".exe")]
            if len(names) != 1:
                sys.exit(f"expected one .exe in {path}, found {names}")
            return z.read(names[0])
    return open(path, "rb").read()


def find_hex_text(data):
    runs = re.findall(rb"(?:[0-9A-Fa-f:\r\n]\x00){1000,}", data)
    if not runs:
        sys.exit("no UTF-16 Intel HEX found")
    return max(runs, key=len).decode("utf-16-le")


def parse(text):
    image = bytearray(APP_END - APP_BASE)
    upper, records, used, eof = 0, 0, 0, False
    for line in re.findall(r":[0-9A-Fa-f]+", text):
        if len(line) % 2 == 0:
            sys.exit(f"odd number of hex digits in record {records + 1}: {line}")
        raw = bytes.fromhex(line[1:])
        if sum(raw) & 0xFF:
            sys.exit(f"bad checksum in record {records + 1}: {line}")
        count, addr, kind, payload = raw[0], int.from_bytes(raw[1:3], "big"), raw[3], raw[4:-1]
        if len(payload) != count:
            sys.exit(f"bad length in record {records + 1}: {line}")
        records += 1
        if kind == 0x00:
            a = (upper << 16) + addr
            if not (APP_BASE <= a and a + count <= APP_END):
                sys.exit(f"data outside the application area at {a:#010x}")
            image[a - APP_BASE:a - APP_BASE + count] = payload
            used += count
        elif kind == 0x04:
            upper = int.from_bytes(payload, "big")
        elif kind == 0x01:
            eof = True
            break
    if not eof:
        sys.exit("no end-of-file record")
    return bytes(image), records, used


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    image, records, used = parse(find_hex_text(read_exe(sys.argv[1])))
    version = re.search(rb"UV Radio V[0-9.]+ [0-9-]+", image)
    with open(sys.argv[2], "wb") as f:
        f.write(image)
    print(f"{records} records, {used} data bytes, version text: "
          f"{version.group().decode() if version else 'not found'}")
    print(f"wrote {sys.argv[2]} ({len(image)} bytes)")


if __name__ == "__main__":
    main()
