#!/usr/bin/env python3
"""Render a mock of the pet screen so its layout and its face can be checked offline.

Mirrors the drawing in system/game_pet.cpp: the terminal chrome (header, rules, four
bracketed meters plus health, a thought, a text menu) and clawd himself -- the body
sprite for the stage, with eyes, a mouth and a mood mark drawn into it.

Both halves of that are worth checking before flashing. The panel is round, so the
usable width at row y is 2*sqrt(180^2-(y-180)^2) and anything wider is behind the
bezel; and the eyes are drawn at coordinates compiled into the sprite header, so a
mistake there puts them on clawd's chin rather than his face.

    tools/preview_pet.py                  # contact sheet of stages and moods
    tools/preview_pet.py --screen         # one full 360x360 screen, bezel masked
    tools/preview_pet.py --screen --stage 3 --anim annoyed --hunger 20
"""

import argparse
import math
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from previewlib import (SCR_W, SCR_H, load_font, draw_text,  # noqa: E402
                        text_centered, text_width, apply_bezel, finish,
                        load_pet_sprites, draw_pet_body)

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
HEALTH_Y = 106
FACE_CY = 174
THOUGHT_Y = 240
RULE2_Y = 262
MENU_Y0, MENU_PITCH = 278, 18
RULE_X0, RULE_X1 = 62, 298
BAR_CHARS = 8

STAGE_NAMES = ["BABY", "CHILD", "TEEN", "ADULT"]
ACT_LABELS = ["FEED", "PLAY", "PET", "SLEEP", "SCRUB", "NAME"]
ANIM_NAMES = ["idle", "happy", "eat", "sleep", "bath", "sick", "annoyed"]

EYE_ROUND, EYE_HAPPY, EYE_SHUT, EYE_CROSS, EYE_ANGRY = range(5)
EMOTE_NONE, EMOTE_HEART, EMOTE_ANGER, EMOTE_SICK, EMOTE_ZZZ = range(5)

# eyes, emote, mouth, bob -- the MOODS table verbatim.
MOODS = {
    "idle": (EYE_ROUND, EMOTE_NONE, False, 2),
    "happy": (EYE_HAPPY, EMOTE_HEART, False, 4),
    "eat": (EYE_SHUT, EMOTE_NONE, True, 3),
    "sleep": (EYE_SHUT, EMOTE_ZZZ, False, 0),
    "bath": (EYE_HAPPY, EMOTE_HEART, False, 3),
    "sick": (EYE_CROSS, EMOTE_SICK, False, 0),
    "annoyed": (EYE_ANGRY, EMOTE_ANGER, False, 4),
}

FONT = load_font(os.path.join(ROOT, "system", "assets_font.h"))
PAL, SPRITES = load_pet_sprites(os.path.join(ROOT, "system", "assets_pet.h"))
INK = PAL[2]


def fill_rect(px, x, y, w, h, color):
    for yy in range(int(y), int(y + h)):
        for xx in range(int(x), int(x + w)):
            if 0 <= xx < SCR_W and 0 <= yy < SCR_H:
                px[xx, yy] = color


def fill_circle(px, cx, cy, r, color):
    for yy in range(int(cy - r) - 1, int(cy + r) + 2):
        for xx in range(int(cx - r) - 1, int(cx + r) + 2):
            if (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r and 0 <= xx < SCR_W and 0 <= yy < SCR_H:
                px[xx, yy] = color


def draw_line(px, x0, y0, x1, y1, color):
    steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(steps + 1):
        t = i / steps
        xx, yy = int(x0 + (x1 - x0) * t), int(y0 + (y1 - y0) * t)
        if 0 <= xx < SCR_W and 0 <= yy < SCR_H:
            px[xx, yy] = color


def draw_eye(px, x, y, r, style, color, dir=1):
    if style == EYE_HAPPY:
        for i in range(-r, r + 1):
            fill_rect(px, x + i, y - (r - abs(i)) // 2, 1, 3, color)
    elif style == EYE_SHUT:
        fill_rect(px, x - r, y - 1, 2 * r + 1, 2, color)
    elif style == EYE_CROSS:
        draw_line(px, x - r, y - r, x + r, y + r, color)
        draw_line(px, x + r, y - r, x - r, y + r, color)
    elif style == EYE_ANGRY:
        for i in range(-r, r + 1):
            t = (i + r) if dir > 0 else (r - i)
            fill_rect(px, x + i, y - r // 2 + t // 2, 1, 2, color)
    else:
        fill_circle(px, x, y, r, color)


def draw_mouth(px, x, y, r, color):
    for i in range(-r, r + 1):
        fill_rect(px, x + i, y, 1, (r - abs(i)) * 3 // 4 + 1, color)


def draw_heart(px, x, y, r, color):
    fill_circle(px, x - r * 0.5, y - r * 0.3, r * 0.55, color)
    fill_circle(px, x + r * 0.5, y - r * 0.3, r * 0.55, color)
    for i in range(-int(r), int(r) + 1):
        fill_rect(px, x + i, y + r * 0.25, 1, (r - abs(i)) * 0.9, color)


def draw_anger(px, x, y, r, color):
    for k in range(4):
        a = math.pi * 0.25 + k * math.pi * 0.5
        for t in range(2):
            o = t * 0.22 + 0.11
            draw_line(px, x + math.cos(a) * r * 0.30 + math.sin(a) * r * o,
                      y + math.sin(a) * r * 0.30 - math.cos(a) * r * o,
                      x + math.cos(a) * r + math.sin(a) * r * o,
                      y + math.sin(a) * r - math.cos(a) * r * o, color)


def draw_sick(px, x, y, r, color):
    fill_rect(px, x - r * 0.28, y - r, r * 0.56, 2 * r, color)
    fill_rect(px, x - r, y - r * 0.28, 2 * r, r * 0.56, color)


def draw_emote(px, emote, x, y):
    if emote == EMOTE_HEART:
        draw_heart(px, x, y, 9.0, COL_WARN)
    elif emote == EMOTE_ANGER:
        draw_anger(px, x, y, 9.0, COL_WARN)
    elif emote == EMOTE_SICK:
        draw_sick(px, x, y, 7.0, COL_WARN)
    elif emote == EMOTE_ZZZ:
        draw_text(px, FONT, "z", x - 6, y - 12, COL_TEXT, 2)
        draw_text(px, FONT, "z", x + 8, y, COL_DIM, 1)


def draw_pet(px, stage, mood, cx=180, cy=FACE_CY):
    eyes, emote, mouth, _bob = MOODS[mood]
    sprite = SPRITES[STAGE_NAMES[stage].lower()]
    w, h, _, _, eye_dx, eye_dy, eye_r = sprite
    top = cy - h // 2
    draw_pet_body(px, PAL, sprite, cx, cy)
    ey = cy + eye_dy
    draw_eye(px, cx - eye_dx, ey, eye_r, eyes, INK, +1)
    draw_eye(px, cx + eye_dx, ey, eye_r, eyes, INK, -1)
    if mouth:
        draw_mouth(px, cx, ey + eye_r * 2, eye_r, INK)
    if emote:
        draw_emote(px, emote, cx + w // 2 - 10, top + 6)


def draw_meter(px, label, value, x, y):
    filled = round(max(0.0, min(100.0, value)) / 100.0 * BAR_CHARS)
    line = "%-6s [" % label + "".join("#" if i < filled else "." for i in range(BAR_CHARS)) + "]"
    draw_text(px, FONT, line, x, y, COL_WARN if value < 30 else COL_ACCENT, 1)


def draw_chrome(px, stage, hunger, energy, happy, clean, health, thought, name="CLAWD"):
    head = "%s  |  %s  |  %02d:%02d" % (name, STAGE_NAMES[stage], 0, 12)
    text_centered(px, FONT, head, HEAD_Y, COL_ACCENT, 1)
    fill_rect(px, RULE_X0, RULE1_Y, RULE_X1 - RULE_X0, 1, COL_RULE)
    draw_meter(px, "HUNGER", hunger, METER_COL_A, METER_ROW0)
    draw_meter(px, "ENERGY", energy, METER_COL_B, METER_ROW0)
    draw_meter(px, "HAPPY", happy, METER_COL_A, METER_ROW1)
    draw_meter(px, "CLEAN", clean, METER_COL_B, METER_ROW1)
    text = "%-6s [" % "HEALTH" + "".join(
        "#" if i < round(max(0.0, min(100.0, health)) / 100.0 * BAR_CHARS) else "."
        for i in range(BAR_CHARS)) + "]"
    draw_text(px, FONT, text, int(180 - text_width(FONT, text, 1) * 0.5), HEALTH_Y, COL_TEXT, 1)
    if thought:
        text_centered(px, FONT, thought, THOUGHT_Y, COL_TEXT, 1)
    fill_rect(px, RULE_X0, RULE2_Y, RULE_X1 - RULE_X0, 1, COL_RULE)
    x = 54
    gap = 14
    for i, label in enumerate(ACT_LABELS):
        if i == 3:                      # the menu wraps at three per row
            x = 54 + ((RULE_X1 - RULE_X0) - (3 * 30 + 2 * gap)) // 4
        row, _ = divmod(i, 3)
        draw_text(px, FONT, label, x, MENU_Y0 + row * MENU_PITCH, COL_ACCENT, 1)
        x += text_width(FONT, label, 1) + gap
        if i == 2:
            x = 54 + ((RULE_X1 - RULE_X0) - (3 * 30 + 2 * gap)) // 4


def screen(args):
    img = Image.new("RGB", (SCR_W, SCR_H), COL_BG)
    px = img.load()
    if args.shake:
        text_centered(px, FONT, "SHAKE  5 LEFT", METER_ROW0, COL_ACCENT, 1)
        text_centered(px, FONT, "hold the badge and shake it", METER_ROW1 + 4, COL_DIM, 1)
        text = "DIRT   [" + "." * BAR_CHARS + "]"
        draw_text(px, FONT, text, int(180 - text_width(FONT, text, 1) * 0.5), HEALTH_Y, COL_ACCENT, 1)
        sprite = SPRITES[STAGE_NAMES[args.stage].lower()]
        rx, ry = sprite[0] * 0.30 + 14, sprite[1] * 0.30 + 14
        for i in range(5):
            ang = -math.pi * 0.5 + i * (2 * math.pi / 5)
            fill_circle(px, 180 + math.cos(ang) * rx, FACE_CY + math.sin(ang) * ry + 4, 5, COL_DIM)
        draw_pet(px, args.stage, args.anim)
        text_centered(px, FONT, "tap to stop", SCR_H - 30, COL_DIM, 1)
    else:
        draw_chrome(px, args.stage, args.hunger, args.energy, args.happy,
                    args.clean, args.health, args.thought)
        draw_pet(px, args.stage, args.anim)
    apply_bezel(img)
    finish(img, args.out, 2)


def sheet(args):
    """Every stage against every mood, in the band the pet actually occupies, so a
    mood that only differs by a couple of pixels can be compared at a glance."""
    cols, rows = len(ANIM_NAMES), 4
    cw, ch = 200, 150
    img = Image.new("RGB", (cols * cw, rows * ch + 26), (0x18, 0x18, 0x20))
    px = img.load()
    draw_text(px, FONT, "stage \\ mood", 8, 9, COL_TEXT, 1)
    for c, mood in enumerate(ANIM_NAMES):
        draw_text(px, FONT, mood, c * cw + 80, 9, COL_TEXT, 1)
    for r, stage in enumerate(range(4)):
        y0 = 26 + r * ch
        draw_text(px, FONT, STAGE_NAMES[stage], 8, y0 + ch // 2 - 4, COL_ACCENT, 1)
        for c, mood in enumerate(ANIM_NAMES):
            sub = Image.new("RGB", (SCR_W, SCR_H), COL_BG)
            draw_chrome(sub.load(), stage, 80, 80, 80, 80, 80, "")
            draw_pet(sub.load(), stage, mood)
            img.paste(sub.crop((0, FACE_CY - ch // 2, cw, FACE_CY + ch // 2)), (c * cw, y0))
        img.paste(Image.new("RGB", (cols * cw, 1), (0x30, 0x30, 0x40)), (0, y0 + ch - 1))
    finish(img, args.out, 1)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--screen", action="store_true", help="one full screen, not a sheet")
    ap.add_argument("--shake", action="store_true", help="the scrub mini-game instead")
    ap.add_argument("--stage", type=int, default=3, choices=list(range(4)))
    ap.add_argument("--anim", default="idle", choices=list(MOODS))
    ap.add_argument("--hunger", type=float, default=72)
    ap.add_argument("--energy", type=float, default=64)
    ap.add_argument("--happy", type=float, default=81)
    ap.add_argument("--clean", type=float, default=45)
    ap.add_argument("--health", type=float, default=70)
    ap.add_argument("--thought", default="just hanging out")
    ap.add_argument("--out", default="build/preview/pet_state.png")
    args = ap.parse_args()
    args.out = os.path.join(ROOT, args.out)
    (screen if args.screen else sheet)(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
