#!/usr/bin/env python3
"""Shared helpers for the offline screen previews.

The panel is a 360 px square behind a round bezel, so usable width shrinks with
distance from the centre and text that is well inside the framebuffer can still be
invisible on the device. Every layout in this project has to satisfy that, and the
cheap way to check is to draw it here first rather than to flash it and ask.

Import as a module from the preview scripts; not meant to be run directly.
"""

import math
import os
import re

from PIL import Image

SCR_W = SCR_H = 360
CX = CY = 180.0
GLYPH_ADVANCE = 7


# --------------------------------------------------------------------- font ---
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


def text_width(font, s, scale=1):
    _, _, cell, _ = font
    return len(s) * GLYPH_ADVANCE * scale - (GLYPH_ADVANCE - cell) * scale


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


def text_centered(px, font, s, cy, color, scale=1):
    draw_text(px, font, s, int(CX - text_width(font, s, scale) * 0.5), cy, color, scale)


# ------------------------------------------------------------------ geometry ---
def visible_half_width(y):
    dy = y - CY
    d2 = 180.0 * 180.0 - dy * dy
    return math.sqrt(d2) if d2 > 0 else 0.0


def apply_bezel(img):
    """Everything outside the round panel is black on the device."""
    px = img.load()
    for y in range(SCR_H):
        for x in range(SCR_W):
            if math.hypot(x + 0.5 - CX, y + 0.5 - CY) > 180.0:
                px[x, y] = (0, 0, 0)


# ------------------------------------------------------------------- sprites ---
def load_pet_sprites(path):
    """Returns (palette, {name: (w, h, rowBytes, bytes, eyeDX, eyeDY, eyeR)})."""
    src = open(path, encoding="utf-8").read()

    pal = []
    m = re.search(r"pet_palette\[PET_PALETTE_COUNT\] = \{(.*?)\};", src, re.S)
    for mm in re.finditer(r"0x([0-9A-F]{4})", m.group(1)):
        v = int(mm.group(1), 16)
        # The C palette is host-order RGB565; expand back to 8-bit channels.
        pal.append((((v >> 11) & 0x1F) * 255 // 31,
                    ((v >> 5) & 0x3F) * 255 // 63,
                    (v & 0x1F) * 255 // 31))

    bits = {}
    for mm in re.finditer(r"pet_bits_(\w+)\[(\d+)\] = \{(.*?)\};", src, re.S):
        bits[mm.group(1)] = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{2})", mm.group(3))]

    sprites = {}
    for mm in re.finditer(
            r"\{ pet_bits_(\w+), (\d+), (\d+), (\d+), (-?\d+), (-?\d+), (\d+) \}", src):
        name = mm.group(1)
        w, h, rb = int(mm.group(2)), int(mm.group(3)), int(mm.group(4))
        sprites[name] = (w, h, rb, bits[name],
                         int(mm.group(5)), int(mm.group(6)), int(mm.group(7)))
    return pal, sprites


def draw_pet_body(px, pal, sprite, cx, cy):
    """2bpp body sprite, drawn 1:1 -- each stage is already at its display size."""
    w, h, rb, data = sprite[:4]
    ox, oy = cx - w // 2, cy - h // 2
    for y in range(h):
        for x in range(w):
            idx = (data[y * rb + (x >> 2)] >> ((x & 3) * 2)) & 3
            if not idx:
                continue
            xx, yy = ox + x, oy + y
            if 0 <= xx < SCR_W and 0 <= yy < SCR_H:
                px[xx, yy] = pal[idx]


# -------------------------------------------------------------------- output ---
def finish(img, out, scale=2):
    os.makedirs(os.path.dirname(out), exist_ok=True)
    img.resize((SCR_W * scale, SCR_H * scale), Image.NEAREST).save(out)
    print(f"wrote {out}")
