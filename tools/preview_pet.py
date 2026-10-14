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
                        draw_text, text_centered, text_width, visible_half_width,
                        apply_bezel, finish)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- must match system/game_pet.cpp -----------------------------------------
COL_BG = (0x0E, 0x0E, 0x16)
COL_TEXT = (0xEC, 0xEC, 0xF4)
COL_DIM = (0x6E, 0x6E, 0x80)
COL_RULE = (0x2A, 0x2A, 0x3A)
COL_HEART_FULL = (0xFF, 0x5A, 0x7A)
COL_HEART_EMPTY = (0x33, 0x33, 0x42)
COL_ACCENT = (0xD9, 0x77, 0x57)

HEAD_Y, RULE1_Y, RULE2_Y = 44, 60, 272
RULE_X0, RULE_X1 = 62, 298
HEART_TOTAL, HEART_PITCH = 4, 13
METER_LABEL_RIGHT, METER_HEART_X = 168, 180
METER_Y0, METER_PITCH = 76, 18

PET_CX, PET_CY = 180.0, 176.0
MSG_Y = 250
MENU_Y0, MENU_PITCH = 288, 18

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


def dim(c, k):
    return tuple(int(v * (1 - k)) for v in c)


def draw_heart(px, cx, cy, filled):
    c = COL_HEART_FULL if filled else COL_HEART_EMPTY
    disc(px, cx - 2, cy - 1, 2.2, c)
    disc(px, cx + 2, cy - 1, 2.2, c)
    for i in range(5):
        rect(px, cx - 3 + i, cy + 1 + i, 7 - 2 * i, 1, c)


def draw_pet(px, sprite, scale, pet_rgb):
    w, h, rb, data = sprite
    bo = int(round(scale))
    ox, oy = PET_CX - w * scale / 2, PET_CY - h * scale / 2
    for y in range(h):
        for x in range(w):
            idx = (data[y * rb + (x >> 1)] >> ((x & 1) * 4)) & 0x0F
            if not idx:
                continue
            rect(px, ox + x * scale, oy + y * scale, bo, bo, pet_rgb[idx])


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
    pal, sprites = load_pet_sprites(os.path.join(ROOT, "system", "assets_pet.h"))
    pet_rgb = [tuple(int(v * 255 // 255) for v in c) for c in pal]

    img = Image.new("RGB", (SCR_W, SCR_H), COL_BG)
    px = img.load()

    head = f"CLAWD PET  |  {STAGE_NAMES[args.stage]}  |  {args.age}"
    text_centered(px, font, head, HEAD_Y, COL_ACCENT, 1)
    rect(px, RULE_X0, RULE1_Y, RULE_X1 - RULE_X0, 1, COL_RULE)

    hh = int(round((100 - args.hunger) / (100 / HEART_TOTAL)))
    fh = int(round((100 - args.fun) / (100 / HEART_TOTAL)))
    for i, (name, val) in enumerate((("HUNGER", hh), ("HAPPY", fh))):
        y = METER_Y0 + i * METER_PITCH
        draw_text(px, font, name, METER_LABEL_RIGHT - text_width(font, name, 1), y, COL_DIM, 1)
        for k in range(HEART_TOTAL):
            draw_heart(px, METER_HEART_X + k * HEART_PITCH, y + 3, k < val)

    name = SPRITE_SLEEP[args.stage] if (args.asleep and args.stage != 0) else SPRITE_NAME[args.stage]
    draw_pet(px, sprites[name], stage_scale(args.stage), pet_rgb)

    if args.poop:
        draw_text(px, font, "MESS", int(PET_CX) + 66, int(PET_CY) + 30, COL_ACCENT, 1)
    if args.sick:
        draw_text(px, font, "SICK", int(PET_CX) - 96, int(PET_CY) + 30, COL_HEART_FULL, 1)

    text_centered(px, font, "clawd played until it puffed", MSG_Y, COL_TEXT, 1)

    rect(px, RULE_X0, RULE2_Y, RULE_X1 - RULE_X0, 1, COL_RULE)

    # two rows of words, four then three
    gap = 14.0
    for row, (first, last) in enumerate(((0, 4), (4, 7))):
        total = sum(text_width(font, GLYPH_NAMES[i].upper(), 1) + gap for i in range(first, last)) - gap
        x = CX - total / 2
        y = MENU_Y0 + row * MENU_PITCH
        for i in range(first, last):
            lab = GLYPH_NAMES[i].upper()
            w = text_width(font, lab, 1)
            if i == args.selected:
                rect(px, x - 4, y - 3, w + 8, 14, dim(COL_ACCENT, 0.65))
            draw_text(px, font, lab, int(x), y,
                      COL_TEXT if i == args.selected else COL_ACCENT, 1)
            x += w + gap

    apply_bezel(img)
    finish(img, args.out, args.scale)

    print()
    print("placement check (needs half-width <= visible half-width - 2):")

    def check(label, row, half):
        hw = visible_half_width(row)
        print(f"  {label:22s} needs {half:6.1f}  visible {hw:6.1f}  -> "
              f"{'fits' if half <= hw - 2 else 'CLIPPED'}")

    check("header line", HEAD_Y + 7, text_width(font, head, 1) / 2)
    check("rule 1", RULE1_Y + 1, max(abs(RULE_X0 - CX), abs(RULE_X1 - CX)))
    check("meter row (HUNGER)", METER_Y0 + 10,
          max(CX - (METER_LABEL_RIGHT - text_width(font, "HUNGER", 1)),
              METER_HEART_X + 3 * HEART_PITCH - CX))
    check("pet sprite", int(PET_CY + SPRITE_W[SPRITE_NAME[args.stage]] * stage_scale(args.stage) / 2),
          SPRITE_W[SPRITE_NAME[args.stage]] * stage_scale(args.stage) / 2)
    check("message line", MSG_Y + 7, text_width(font, "clawd played until it puffed", 1) / 2)
    check("rule 2", RULE2_Y + 1, max(abs(RULE_X0 - CX), abs(RULE_X1 - CX)))
    for row, (first, last) in enumerate(((0, 4), (4, 7))):
        total = sum(text_width(font, GLYPH_NAMES[i].upper(), 1) + gap for i in range(first, last)) - gap
        check(f"menu row {row + 1}", MENU_Y0 + row * MENU_PITCH + 7, total / 2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
