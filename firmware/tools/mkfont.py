"""Generate src/font.h: 1-bit glyphs for ASCII 0x20-0x7E from a TrueType font."""
import sys
from PIL import Image, ImageDraw, ImageFont

TTF = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
W, H, SIZE = 16, 24, 20

font = ImageFont.truetype(TTF, SIZE)
ascent, descent = font.getmetrics()
top = (H - (ascent + descent)) // 2

rows = []
for code in range(0x20, 0x7F):
    img = Image.new("1", (W, H), 0)
    d = ImageDraw.Draw(img)
    ch = chr(code)
    left = (W - font.getlength(ch)) / 2
    d.text((left, top), ch, font=font, fill=1)
    words = []
    for y in range(H):
        v = 0
        for x in range(W):
            if img.getpixel((x, y)):
                v |= 0x8000 >> x
        words.append(v)
    rows.append((ch, words))

out = sys.argv[1] if len(sys.argv) > 1 else "src/font.h"
with open(out, "w") as f:
    f.write("#ifndef FONT_GLYPHS_H\n#define FONT_GLYPHS_H\n#include <stdint.h>\n\n")
    f.write(f"#define FONT_W {W}\n#define FONT_H {H}\n#define FONT_FIRST 0x20\n#define FONT_LAST 0x7E\n\n")
    f.write(f"static const uint16_t font_glyphs[{len(rows)}][{H}] = {{\n")
    for ch, words in rows:
        label = ch if ch not in "\\'" else "\\" + ch
        f.write("    { " + ", ".join(f"0x{w:04X}" for w in words) + f" }}, /* '{label}' */\n")
    f.write("};\n\n#endif\n")
print(f"wrote {out}: {len(rows)} glyphs, {W}x{H}")
