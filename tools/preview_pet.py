#!/usr/bin/env python3
"""Render a mock of the pet screen so its layout can be checked offline.

Mirrors the constants in system/game_pet.cpp. If you change the layout there, change
it here too -- the point is to see the round-bezel clipping before flashing rather
than after.

    tools/preview_pet.py [--stage 3] [--asleep] [--out build/preview-pet.png]
"""

import argparse
import math
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from previewlib import (SCR_W, SCR_H, CX, CY, load_font, load_pet_sprites,  # noqa: E402
                        draw_text, text_centered, text_width, visible_half_width,
                        draw_pet_sprite, apply_bezel, finish)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- must match system/game_pet.cpp -----------------------------------------
BAR_X, BAR_W, BAR_H = 124, 116, 7
BAR_Y0, BAR_PITCH = 30, 14
PET_CX, PET_CY, PET_SCALE = 180.0, 168.0, 6.0
MSG_Y = 234
BTN_R = 20.0
BTN_ROW1_Y, BTN_ROW2_Y, BTN_PITCH = 268.0, 310.0, 46.0

COL_BG = (0x0B, 0x0B, 0x12)
COL_DOT = (0x18, 0x18, 0x2A)
COL_TRACK = (0x20, 0x20, 0x32)
COL_TEXT = (0xD8, 0xD8, 0xE8)
COL_DIM = (0x6A, 0x6A, 0x86)
BAR_COLS = [(0xE8, 0x9B, 0x3C), (0x5A, 0x9E, 0xE8), (0x4C, 0xC4, 0x7A), (0xC8, 0x6B, 0xD8)]
BTN_COLS = [(0x4C, 0xC4, 0x7A), (0xC8, 0xA0, 0x4C), (0xE8, 0x6B, 0xA0), (0x4C, 0x9E, 0xE8),
            (0x9C, 0x7A, 0x4C), (0xE0, 0x50, 0x50), (0x8A, 0x6B, 0xD8)]
BTN_LABELS = ["FEED", "SNACK", "PLAY", "WASH", "CLEAN", "MED", "SLEEP"]
STAGE_NAMES = ["EGG", "BABY", "TEEN", "ADULT"]


def dim(c, k):
    return tuple(int(v * (1 - k)) for v in c)


def fill_rect(px, x, y, w, h, col):
    for yy in range(max(0, y), min(SCR_H, y + h)):
        for xx in range(max(0, x), min(SCR_W, x + w)):
            px[xx, yy] = col


def disc(px, cx, cy, r, col, outline=None, ring=2):
    for yy in range(int(cy - r), int(cy + r) + 1):
        for xx in range(int(cx - r), int(cx + r) + 1):
            if not (0 <= xx < SCR_W and 0 <= yy < SCR_H):
                continue
            d = math.hypot(xx - cx, yy - cy)
            if d <= r - ring:
                px[xx, yy] = col
            elif outline and d <= r:
                px[xx, yy] = outline


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage", type=int, default=3)
    ap.add_argument("--asleep", action="store_true")
    ap.add_argument("--hunger", type=float, default=42)
    ap.add_argument("--fun", type=float, default=38)
    ap.add_argument("--clean", type=float, default=68)
    ap.add_argument("--energy", type=float, default=74)
    ap.add_argument("--sick", action="store_true")
    ap.add_argument("--poop", action="store_true")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "preview-pet.png"))
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args()

    font = load_font(os.path.join(ROOT, "system", "assets_font.h"))
    pal, sprites = load_pet_sprites(os.path.join(ROOT, "system", "assets_pet.h"))

    img = Image.new("RGB", (SCR_W, SCR_H), COL_BG)
    px = img.load()
    for y in range(22, SCR_H, 45):
        for x in range(22, SCR_W, 45):
            fill_rect(px, x, y, 2, 2, COL_DOT)

    # stat bars -- hunger and boredom are drawn inverted, as the firmware does
    vals = [args.hunger, args.fun, args.clean, args.energy]
    for i, v in enumerate(vals):
        y = BAR_Y0 + i * BAR_PITCH
        fill_rect(px, BAR_X - 12, y, 8, 7, dim(BAR_COLS[i], 0.4))
        fill_rect(px, BAR_X, y, BAR_W, BAR_H, COL_TRACK)
        frac = (100 - v) / 100 if i < 2 else v / 100
        fill_rect(px, BAR_X, y, int(BAR_W * frac + 0.5), BAR_H, BAR_COLS[i])

    age = 402
    head = f"{STAGE_NAMES[args.stage]}  {age // 60:02d}:{age % 60:02d}"
    text_centered(px, font, head, 92, COL_DIM, 1)

    name = ["egg", "baby", "teen", "adult"][args.stage]
    if args.asleep and args.stage != 0:
        name = ["egg", "babySleep", "teenSleep", "adultSleep"][args.stage]
    draw_pet_sprite(px, pal, sprites[name], PET_CX, PET_CY, PET_SCALE)

    if args.poop:
        fill_rect(px, int(PET_CX) + 74, int(PET_CY) + 30, 13, 9, (0x9C, 0x7A, 0x4C))
    if args.sick:
        disc(px, PET_CX - 82, PET_CY + 33, 7, (0xE0, 0x50, 0x50))

    text_centered(px, font, "clawd played until it puffed", MSG_Y, COL_TEXT, 1)

    # buttons: 4 on the first row, 3 on the second
    coords = []
    for i in range(4):
        coords.append((PET_CX + (i - 1.5) * BTN_PITCH, BTN_ROW1_Y, i, True))
    for i in range(3):
        enabled = {0: args.poop, 1: args.sick}.get(i, True)   # clean, med
        coords.append((PET_CX + (i - 1.0) * BTN_PITCH, BTN_ROW2_Y, 4 + i, enabled))
    for cx, cy, idx, enabled in coords:
        c = BTN_COLS[idx] if enabled else dim(BTN_COLS[idx], 0.72)
        disc(px, cx, cy, BTN_R, dim(c, 0.78), outline=c, ring=2)
        lab = BTN_LABELS[idx]
        draw_text(px, font, lab, int(cx - text_width(font, lab, 1) * 0.5), int(cy - 3),
                  COL_TEXT if enabled else COL_DIM, 1)

    apply_bezel(img)
    finish(img, args.out, args.scale)

    print()
    print("placement check (needs half-width <= visible half-width - 2):")
    def check(label, cy, half_w):
        hw = visible_half_width(cy)
        ok = half_w <= hw - 2
        print(f"  {label:26s} needs {half_w:6.1f}  visible {hw:6.1f}  -> "
              f"{'fits' if ok else 'CLIPPED'}")

    check("stat bars (right end)", BAR_Y0 + 3 * BAR_PITCH + BAR_H - 1, BAR_X + BAR_W - CX)
    check("stat bar swatches", BAR_Y0 + 3 * BAR_PITCH + BAR_H - 1, CX - (BAR_X - 12))
    for cx, cy, idx, _ in coords:
        check(f"button {BTN_LABELS[idx]}", int(cy + BTN_R), abs(cx - CX) + BTN_R)
    check("message line", MSG_Y + 7, text_width(font, "clawd played until it puffed", 1) / 2)
    check("age line", 92 + 7, text_width(font, head, 1) / 2)
    hw = visible_half_width(int(PET_CY) - int(18 * PET_SCALE / 2))
    print(f"  {'pet sprite':26s} needs {20 * PET_SCALE / 2:6.1f}  visible {hw:6.1f}  -> "
          f"{'fits' if 20 * PET_SCALE / 2 <= hw else 'CLIPPED'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
