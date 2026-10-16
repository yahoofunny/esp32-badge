#!/usr/bin/env python3
"""Render a mock of the launcher screen so layout can be checked without the board.

Why this exists: the panel is a 360 px square behind a round bezel, so the usable
width at any row is 2*sqrt(180^2 - (y-180)^2) and text that looks fine in the
framebuffer can be invisible on the device. Several rounds of this project were spent
shipping layout changes and asking the user whether they looked right, which is a slow
and unreliable way to find out. Anything about the *static* composition can be checked
here instead.

It re-implements the launcher's layout constants and the font, not the icon art (the
games draw theirs procedurally), so treat it as a composition check rather than a
pixel-exact preview.

    tools/preview_launcher.py            # writes build/preview-launcher.png
"""

import argparse
import math
import os
import re

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SCR_W = SCR_H = 360
CX = CY = 180.0
ORBIT_X, ORBIT_Y = 112.0, 66.0
ICON_MIN, ICON_MAX = 46.0, 74.0
GLYPH_ADVANCE = 7
CELL = 8

COL_BG = (0x0B, 0x0B, 0x12)
COL_DOT = (0x18, 0x18, 0x2A)
COL_TRACK = (0x22, 0x22, 0x46)
COL_LABEL = (0xD8, 0xD8, 0xE8)
COL_HINT = (0x5A, 0x5A, 0x72)

# The entries the firmware actually registers, in ring order.
ENTRIES = [
    ("CLAWD ROLL", (0xE8, 0x3B, 0x3B)),
    ("CLAWD TAP", (0x2E, 0xC4, 0x6B)),
    ("CLAWD PET", (0xD9, 0x77, 0x57)),
    ("PHOTOS", (0x7A, 0x8B, 0xF0)),
    ("SETTINGS", (0xE6, 0xB4, 0x50)),
]


def load_font(path):
    src = open(path, encoding="utf-8").read()

    def define(name):
        m = re.search(rf"#define {name}\s+(-?\d+)", src)
        return int(m.group(1))

    first, last, cell = define("FONT_FIRST"), define("FONT_LAST"), define("FONT_CELL")
    glyphs = {}
    for m in re.finditer(r"\{(0x[0-9A-F]{2}(?:, 0x[0-9A-F]{2}){7})\},\s+//\s+(\d+)", src):
        glyphs[int(m.group(2))] = [int(x, 16) for x in m.group(1).split(", ")]
    return first, last, cell, glyphs


def draw_text(px, font, s, x, y, color, scale=1):
    first, last, cell, glyphs = font
    for ch in s:
        c = ord(ch)
        if first <= c <= last:
            rows = glyphs[c]
            for gy in range(cell):
                bits = rows[gy]
                if not bits:
                    continue
                for gx in range(cell):
                    if not (bits & (0x80 >> gx)):
                        continue
                    for dy in range(scale):
                        for dx in range(scale):
                            xx, yy = x + gx * scale + dx, y + gy * scale + dy
                            if 0 <= xx < SCR_W and 0 <= yy < SCR_H:
                                px[xx, yy] = color
        x += GLYPH_ADVANCE * scale


def text_width(font, s, scale=1):
    _, _, cell, _ = font
    return len(s) * GLYPH_ADVANCE * scale - (GLYPH_ADVANCE - cell) * scale


def text_centered(px, font, s, cy, color, scale=1):
    draw_text(px, font, s, int(CX - text_width(font, s, scale) * 0.5), cy, color, scale)


def visible_half_width(y):
    dy = y - CY
    d2 = 180.0 * 180.0 - dy * dy
    return math.sqrt(d2) if d2 > 0 else 0.0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--font", default=os.path.join(ROOT, "system", "assets_font.h"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "preview-launcher.png"))
    ap.add_argument("--scale", type=int, default=2, help="output magnification")
    args = ap.parse_args()

    font = load_font(args.font)

    img = Image.new("RGB", (SCR_W, SCR_H), COL_BG)
    px = img.load()

    # floor dots
    for y in range(22, SCR_H, 45):
        for x in range(22, SCR_W, 45):
            for dy in range(2):
                for dx in range(2):
                    if x + dx < SCR_W and y + dy < SCR_H:
                        px[x + dx, y + dy] = COL_DOT

    n = len(ENTRIES)
    step = 2.0 * math.pi / n

    # the carousel track
    for i in range(97):
        a0 = i / 96 * 2 * math.pi
        a1 = (i + 1) / 96 * 2 * math.pi
        for t in range(0, 11):
            a = a0 + (a1 - a0) * t / 10
            x = int(CX + ORBIT_X * math.cos(a))
            y = int(CY + ORBIT_Y * math.sin(a))
            if 0 <= x < SCR_W and 0 <= y < SCR_H:
                px[x, y] = COL_TRACK

    # icons, painted back to front
    order = sorted(range(n), key=lambda i: math.sin(-math.pi / 2 + i * step))
    for i in order:
        a = -math.pi / 2 + i * step
        x, y = CX + ORBIT_X * math.cos(a), CY + ORBIT_Y * math.sin(a)
        t = 1.0 - (y - (CY - ORBIT_Y)) / (2 * ORBIT_Y)
        t = min(1.0, max(0.0, t))
        size = ICON_MIN + (ICON_MAX - ICON_MIN) * t
        r = size * 0.5
        accent = ENTRIES[i][1]
        for yy in range(int(y - r), int(y + r) + 1):
            for xx in range(int(x - r), int(x + r) + 1):
                if not (0 <= xx < SCR_W and 0 <= yy < SCR_H):
                    continue
                d = math.hypot(xx - x, yy - y)
                if d <= r:
                    k = 0.72 - 0.42 * t
                    px[xx, yy] = tuple(int(c * (1 - k)) for c in accent)

    # selected entry parked at the top, as the firmware does
    sel = 0
    sel_angle = -math.pi / 2
    sx = CX + ORBIT_X * math.cos(sel_angle)
    sy = CY + ORBIT_Y * math.sin(sel_angle)
    if 0 <= int(sy) < SCR_H:
        px[int(sx), int(sy)] = (255, 255, 255)

    text_centered(px, font, ENTRIES[sel][0], int(CY + ORBIT_Y) + 34, COL_LABEL, 2)
    text_centered(px, font, "drag ring - tap", 330, COL_HINT, 1)

    # bezel: everything outside the round panel is not visible on the device
    for y in range(SCR_H):
        for x in range(SCR_W):
            if math.hypot(x + 0.5 - CX, y + 0.5 - CY) > 180.0:
                px[x, y] = (0, 0, 0)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    img.resize((SCR_W * args.scale, SCR_H * args.scale), Image.NEAREST).save(args.out)
    print(f"wrote {args.out}")

    # and a textual check of the placements that matter
    print()
    print("placement check (centre-out, minus a 2 px margin):")
    for label, s, y, sc in [("name", ENTRIES[sel][0], int(CY + ORBIT_Y) + 34, 2),
                            ("hint", "drag ring - tap", 330, 1)]:
        w = text_width(font, s, sc)
        worst = y + CELL * sc - 1
        hw = visible_half_width(worst)
        fits = (w / 2.0) <= hw - 2
        print(f"  {label:5s} '{s}'  {w:3d}px wide, lowest row {worst}, "
              f"needs {w/2:5.1f}, visible {hw:5.1f}  -> {'fits' if fits else 'CLIPPED'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
