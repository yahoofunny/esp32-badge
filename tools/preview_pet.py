#!/usr/bin/env python3
"""Render a mock of the pet screen so its layout can be checked offline.

Mirrors the drawing in system/game_pet.cpp: the terminal layout with a header, rules,
four bracketed meters plus health, the emoticon face, a thought, and a text menu.

The panel is round, so the usable width at row y is 2*sqrt(180^2 - (y-180)^2) and
anything wider is hidden behind the bezel. Checking that here is far cheaper than
flashing a build and looking at the device.

    tools/preview_pet.py [--stage 2] [--anim happy] [--hunger 20]
"""

import argparse
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from previewlib import (SCR_W, SCR_H, CX, CY, load_font, draw_text,  # noqa: E402
                        text_centered, text_width, visible_half_width,
                        apply_bezel, finish)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# --- must match system/game_pet.cpp -----------------------------------------
COL_BG = (0x0E, 0x0E, 0x16)
COL_TEXT = (0xEC, 0xEC, 0xF4)
COL_DIM = (0x6E, 0x6E, 0x80)
COL_RULE = (0x2A, 0x2A, 0x3A)
COL_ACCENT = (0xD9, 0x77, 0x57)
COL_WARN = (0xE0, 0x50, 0x50)

HEAD_Y = 44
RULE1_Y = 58
METER_COL_A, METER_COL_B = 54, 196
METER_ROW0, METER_ROW1 = 74, 88
HEALTH_Y = 104
FACE_Y, FACE_SCALE = 152, 4
THOUGHT_Y = 238
RULE2_Y = 262
MENU_Y0, MENU_PITCH = 278, 18
RULE_X0, RULE_X1 = 62, 298
BAR_CHARS = 8

STAGE_NAMES = ["BABY", "CHILD", "TEEN", "ADULT"]
ACT_LABELS = ["FEED", "PLAY", "PET", "SLEEP", "BATHE", "NAME"]

FACES = {
    "idle":  ["(^_^)", "(-_-)", "(^_^)", "(o_o)", "(^_^)", "(^o^)"],
    "happy": ["\\(^o^)/", "(*^_^*)", "\\(^o^)/", "(^_^)b"],
    "eat":   ["(^o^)", "(^O^)", "(^o^)", "(^_^)"],
    "sleep": ["(-_-)zZ", "(-.-)zzZ", "(-_-)zZ", "(u_u)"],
    "bath":  ["(~_~)", "(*_*)", "(~_~)", "(^_^)"],
    "sick":  ["(x_x)", "(u_u)", "(x_x)", "(@_@)"],
}


def meter(label, value):
    filled = int(round(max(0, min(100, value)) / 100 * BAR_CHARS))
    return f"{label:<6} [" + "#" * filled + "." * (BAR_CHARS - filled) + "]"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage", type=int, default=2)
    ap.add_argument("--anim", default="idle", choices=sorted(FACES))
    ap.add_argument("--frame", type=int, default=0)
    ap.add_argument("--name", default="CLAWD")
    ap.add_argument("--age", default="02:14")
    ap.add_argument("--hunger", type=float, default=68)
    ap.add_argument("--happiness", type=float, default=74)
    ap.add_argument("--energy", type=float, default=56)
    ap.add_argument("--clean", type=float, default=83)
    ap.add_argument("--health", type=float, default=71)
    ap.add_argument("--thought", default="just hanging out")
    ap.add_argument("--selected", type=int, default=-1)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "preview-pet.png"))
    ap.add_argument("--scale", type=int, default=2)
    args = ap.parse_args()

    font = load_font(os.path.join(ROOT, "system", "assets_font.h"))
    img = Image.new("RGB", (SCR_W, SCR_H), COL_BG)
    px = img.load()

    head = f"{args.name}  |  {STAGE_NAMES[args.stage]}  |  {args.age}"
    text_centered(px, font, head, HEAD_Y, COL_ACCENT, 1)
    draw_text(px, font, "", RULE_X0, RULE1_Y, COL_RULE)
    for x in range(RULE_X0, RULE_X1):
        px[x, RULE1_Y] = COL_RULE

    def draw_meter(label, value, x, y):
        line = meter(label, value)
        draw_text(px, font, line, x, y, COL_WARN if value < 30 else COL_ACCENT, 1)
        return text_width(font, line, 1)

    draw_meter("HUNGER", args.hunger, METER_COL_A, METER_ROW0)
    draw_meter("ENERGY", args.energy, METER_COL_B, METER_ROW0)
    draw_meter("HAPPY", args.happiness, METER_COL_A, METER_ROW1)
    draw_meter("CLEAN", args.clean, METER_COL_B, METER_ROW1)

    hline = meter("HEALTH", args.health)
    draw_text(px, font, hline, int(CX - text_width(font, hline, 1) / 2), HEALTH_Y, COL_TEXT, 1)

    face = FACES[args.anim][args.frame % len(FACES[args.anim])]
    draw_text(px, font, face, int(CX - text_width(font, face, FACE_SCALE) / 2), FACE_Y,
              COL_ACCENT, FACE_SCALE)

    text_centered(px, font, args.thought, THOUGHT_Y, COL_TEXT, 1)
    for x in range(RULE_X0, RULE_X1):
        px[x, RULE2_Y] = COL_RULE

    per_row, gap = 3, 16.0
    for row in range(2):
        first, last = row * per_row, min(row * per_row + per_row, len(ACT_LABELS))
        total = sum(text_width(font, ACT_LABELS[i], 1) + gap for i in range(first, last)) - gap
        x = CX - total / 2
        y = MENU_Y0 + row * MENU_PITCH
        for i in range(first, last):
            lab = ACT_LABELS[i]
            w = text_width(font, lab, 1)
            if i == args.selected:
                for yy in range(int(y) - 3, int(y) + 11):
                    for xx in range(int(x) - 4, int(x + w) + 4):
                        if 0 <= xx < SCR_W and 0 <= yy < SCR_H:
                            px[xx, yy] = tuple(int(v * 0.35) for v in COL_ACCENT)
            draw_text(px, font, lab, int(x), int(y),
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
    check("meter row 0 (right col)", METER_ROW0 + 7,
          METER_COL_B + text_width(font, meter("ENERGY", args.energy), 1) - CX)
    check("meter row 0 (left col)", METER_ROW0 + 7,
          CX - METER_COL_A)
    check("health line", HEALTH_Y + 7, text_width(font, hline, 1) / 2)
    check("face", FACE_Y + 8 * FACE_SCALE, text_width(font, face, FACE_SCALE) / 2)
    check("thought line", THOUGHT_Y + 7, text_width(font, args.thought, 1) / 2)
    check("rule 2", RULE2_Y + 1, max(abs(RULE_X0 - CX), abs(RULE_X1 - CX)))
    for row in range(2):
        first, last = row * per_row, min(row * per_row + per_row, len(ACT_LABELS))
        total = sum(text_width(font, ACT_LABELS[i], 1) + gap for i in range(first, last)) - gap
        check(f"menu row {row + 1}", MENU_Y0 + row * MENU_PITCH + 7, total / 2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
