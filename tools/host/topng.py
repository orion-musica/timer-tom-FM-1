# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
"""Convert the harness's raw RGB565 LCD dumps into PNG files (scaled 2x),
with the 41 panel LEDs drawn as a strip under the screen."""
import pathlib
import sys

from PIL import Image, ImageDraw

def convert(raw: pathlib.Path) -> None:
    data = raw.read_bytes()
    img = Image.new("RGB", (240, 240))
    px = img.load()
    for i in range(240 * 240):
        v = data[2 * i] | (data[2 * i + 1] << 8)
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        px[i % 240, i // 240] = (r * 255 // 31, g * 255 // 63, b * 255 // 31)
    big = img.resize((480, 480), Image.NEAREST)
    leds = raw.with_suffix(".leds")
    if leds.exists():
        states = leds.read_text().strip()
        canvas = Image.new("RGB", (480, 540), (20, 26, 19))
        canvas.paste(big, (0, 0))
        draw = ImageDraw.Draw(canvas)
        for i, s in enumerate(states):
            # Buttons 0..13 on the first row, keys 14..40 on the second.
            row, col = (0, i) if i < 14 else (1, i - 14)
            x, y = 6 + col * 17, 488 + row * 24
            draw.rectangle([x, y, x + 13, y + 17], fill=(217, 255, 228) if s == "1" else (42, 54, 39))
        big = canvas
    big.save(raw.with_suffix(".png"))

for path in sorted(pathlib.Path(sys.argv[1]).rglob("*.rgb565")):
    convert(path)
    print("wrote", path.with_suffix(".png"))
