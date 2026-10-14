#!/usr/bin/env python3
"""Render a mock of the pet screen so its layout can be checked offline.

Mirrors the drawing in system/game_pet.cpp: the monochrome LCD look, the two heart
meters, the sprite in a single ink colour with the eyes punched out, and the row of
action pictograms with the selected tile inverted.

If you change the layout there, change it here too -- the point is to see round-bezel
clipping and the overall composition before flashing rather than after.

    tools/preview_pet.py [--stage 1|2|3] [--asleep] [--sick] [--poop] [--selected N]
"""

import argparse
import math
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from previewlib import (SCR_W, SCR_H, CX, CY, load_font, load_pet_sprites,  # noqa: E402
                        text_centered, text_width, visible_half_width,
                        apply_bezel, finish)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- must match system/game_pet.cpp -----------------------------------------
LCD_BG = (0xA8, 0xB4, 0x8C)
LCD_INK = (0x1A, 0x1E, 0x14)
LCD_DIM = (0x7E, 0x8A, 0x66)

HEART_TOTAL, HEART_PITCH, HEART_Y = 4, 13, 52
HEART_LEFT_X, HEART_RIGHT_X = 62, 259

PET_CX, PET_CY = 180.0, 172.0
MSG_Y = 266

ICON_PITCH, ICON_Y, ICON_HALF = 33, 300, 13

STAGE_NAMES = ["EGG", "BABY", "TEEN", "ADULT"]
SPRITE_NAME = ["egg", "baby", "teen", "adult"]
SPRITE_SLEEP = ["egg", "babySleep", "teenSleep", "adultSleep"]
SPRITE_W = {"egg": 16, "baby": 16, "teen": 16, "adult": 20}


def rect(px, x, y, w, h, c):
    x, y, w, h = int(round(x)), int(round(y)), int(round(w)), int(round(h))
    for yy in range(max(0, y), min(SCR_H, y + h)):
        for xx in range(max(0, x), min(SCR_W, x + w)):
            px[xx, yy] = c


def disc(px, cx, cy, r, c):
    for yy in range(int(cy - r), int(cy + r) + 1):
        for xx in range(int(cx - r), int(cx + r) + 1):
            if 0 <= xx < SCR_W and 0 <= yy < SCR_H and math.hypot(xx - cx, yy - cy) <= r:
                px[xx, yy] = c


# --- pictograms, mirroring the glyph_* functions in the firmware -------------
def g_bowl(px, cx, cy, c):
    rect(px, cx - 5, cy - 1, 10, 1, c)
    for i in range(9):
        rect(px, cx - 4 + i, cy + abs(i - 4) // 2, 1, 3, c)


def g_candy(px, cx, cy, c):
    disc(px, cx, cy, 3.5, c)
    rect(px, cx - 6, cy - 1, 3, 2, c)
    rect(px, cx + 4, cy - 1, 3, 2, c)


def g_ball(px, cx, cy, c):
    disc(px, cx, cy, 4.5, c)


def g_drop(px, cx, cy, c):
    for i in range(8):
        rect(px, cx - i // 2, cy - 4 + i, 1 + i, 1, c)


def g_broom(px, cx, cy, c):
    rect(px, cx, cy - 6, 1, 6, c)
    for i in range(5):
        rect(px, cx - 3 + i, cy + i // 2, 7 - i, 1, c)


def g_cross(px, cx, cy, c):
    rect(px, cx - 1, cy - 4, 3, 9, c)
    rect(px, cx - 4, cy - 1, 9, 3, c)


def g_zzz(px, cx, cy, c):
    rect(px, cx - 4, cy - 3, 7, 1, c)
    rect(px, cx + 2, cy - 2, 1, 1, c)
    rect(px, cx - 3, cy - 1, 7, 1, c)
    rect(px, cx - 3, cy + 2, 7, 1, c)


GLYPHS = [g_bowl, g_candy, g_ball, g_drop, g_broom, g_cross, g_zzz]
GLYPH_NAMES = ["feed", "snack", "play", "wash", "clean", "med", "sleep"]


def stage_scale(stage):
    return 6 if stage >= 3 else (5 if stage == 2 else 4)


def draw_heart(px, cx, cy, filled):
    c = LCD_INK if filled else LCD_DIM
    disc(px, cx - 2, cy - 1, 2.2, c)
    disc(px, cx + 2, cy - 1, 2.2, c)
    for i in range(5):
        rect(px, cx - 3 + i, cy + 1 + i, 7 - 2 * i, 1, c)


def draw_pet(px, sprite, scale):
    w, h, rb, data = sprite
    bo = int(round(scale))
    ox, oy = PET_CX - w * scale / 2, PET_CY - h * scale / 2
    for y in range(h):
        for x in range(w):
            idx = (data[y * rb + (x >> 1)] >> ((x & 1) * 4)) & 0x0F
            if not idx:
                continue
            rect(px, ox + x * scale, oy + y * scale, bo, bo,
                 LCD_INK if idx == 1 else LCD_BG)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage", type=int, default=2)
    ap.add_argument("--asleep", action="store_true")
    ap.add_argument("--hunger", type=float, default=35)
    ap.add_argument("--fun", type=float, default=58)
    ap.add_argument("--sick", action="store_true")
    ap.add_argument("--poop", action="store_true")
    ap.add_argument("--selected", type=int, default=-1)
    ap.add_argument("--age", default="02:14")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "preview-pet.png"))
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args()

    font = load_font(os.path.join(ROOT, "system", "assets_font.h"))
    _, sprites = load_pet_sprites(os.path.join(ROOT, "system", "assets_pet.h"))

    img = Image.new("RGB", (SCR_W, SCR_H), LCD_BG)
    px = img.load()

    hh = int(round((100 - args.hunger) / (100 / HEART_TOTAL)))
    fh = int(round((100 - args.fun) / (100 / HEART_TOTAL)))
    for i in range(HEART_TOTAL):
        draw_heart(px, HEART_LEFT_X + i * HEART_PITCH, HEART_Y, i < hh)
        draw_heart(px, HEART_RIGHT_X + i * HEART_PITCH, HEART_Y, i < fh)

    head = f"{STAGE_NAMES[args.stage]}  {args.age}"
    text_centered(px, font, head, 84, LCD_INK, 1)

    name = SPRITE_SLEEP[args.stage] if (args.asleep and args.stage != 0) else SPRITE_NAME[args.stage]
    draw_pet(px, sprites[name], stage_scale(args.stage))

    if args.poop:
        disc(px, PET_CX + 84, PET_CY + 32, 5, LCD_INK)
        rect(px, PET_CX + 79, PET_CY + 37, 11, 2, LCD_INK)
    if args.sick:
        g_cross(px, PET_CX - 84, PET_CY + 32, LCD_INK)

    text_centered(px, font, "clawd played until it puffed", MSG_Y, LCD_INK, 1)

    x0 = CX - (len(GLYPHS) - 1) * ICON_PITCH / 2
    for i, fn in enumerate(GLYPHS):
        cx = x0 + i * ICON_PITCH
        if i == args.selected:
            rect(px, cx - ICON_HALF, ICON_Y - ICON_HALF,
                 2 * ICON_HALF + 1, 2 * ICON_HALF + 1, LCD_INK)
            fn(px, cx, ICON_Y, LCD_BG)
        else:
            fn(px, cx, ICON_Y, LCD_INK)

    apply_bezel(img)
    finish(img, args.out, args.scale)

    print()
    print("placement check (needs half-width <= visible half-width - 2):")

    def check(label, row, half):
        hw = visible_half_width(row)
        print(f"  {label:22s} needs {half:6.1f}  visible {hw:6.1f}  -> "
              f"{'fits' if half <= hw - 2 else 'CLIPPED'}")

    check("heart meters", HEART_Y + 6,
          max(CX - HEART_LEFT_X, HEART_RIGHT_X + 3 * HEART_PITCH - CX))
    check("age line", 84 + 7, text_width(font, head, 1) / 2)
    check("message line", MSG_Y + 7, text_width(font, "clawd played until it puffed", 1) / 2)
    check("icon row", ICON_Y + ICON_HALF, abs(x0 - CX) + ICON_HALF)
    check("pet sprite", int(PET_CY + SPRITE_W[SPRITE_NAME[args.stage]] * stage_scale(args.stage) / 2),
          SPRITE_W[SPRITE_NAME[args.stage]] * stage_scale(args.stage) / 2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
