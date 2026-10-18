#!/usr/bin/env python3
"""Render the on-device help pages on the host, so the text can be read without the badge.

The pages are Chinese, drawn from a 16x16 bitmap font generated for exactly these
characters. That is a rendering path no other preview covers, and a missing glyph or a line
too wide for the round panel is not something the serial log can tell you about. This reads
the pages out of system/game_help.cpp and the glyphs out of system/assets_cjk.h, so what is
checked here is what the firmware carries.

    tools/preview_help.py                 # the six pages that matter most
    tools/preview_help.py 0 1 2 3         # specific pages, side by side
    tools/preview_help.py --all           # every page, two images' worth at a time
"""

import argparse
import os
import re
import sys

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from previewlib import SCR_W, SCR_H, load_font  # noqa: E402

CPP = os.path.join(ROOT, "system", "game_help.cpp")
CJK = os.path.join(ROOT, "system", "assets_cjk.h")
FONT = os.path.join(ROOT, "system", "assets_font.h")

CJK_SCALE, ASCII_SCALE, GLYPH = 1, 3, 24
LINE_PITCH, TITLE_Y, DOTS_Y, HINT_Y = 36, 44, 296, 320


def load_pages():
    src = open(CPP, encoding="utf-8").read()
    block = src[src.index("LINES[][4] = {"):]
    block = block[:block.index("\n};")]
    pages = []
    for line in block.splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        pages.append(re.findall(r'"([^"]*)"', line))
    return pages, re.findall(r'"(怎么玩|点一下 下一页)"', src)


def load_glyphs():
    src = open(CJK, encoding="utf-8").read()
    cps = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{4}),",
                                         re.search(r"cjk_codepoints\[CJK_COUNT\] = \{(.*?)\};",
                                                   src, re.S).group(1))]
    rows = re.findall(r"\{ (0x[0-9A-F]{6}, .*?) \},\s+//", src)
    glyphs = {}
    for cp, row in zip(cps, rows):
        glyphs[cp] = [int(v, 16) for v in re.findall(r"0x([0-9A-F]{6})", row)]
    return glyphs


def draw_utf8(px, glyphs, font, s, y, colour):
    def advance(cp):
        return 7 * ASCII_SCALE if cp < 0x80 else GLYPH * CJK_SCALE

    # width first, so the line can be centred
    width = 0
    for ch in s:
        width += advance(ord(ch))
    x = int(SCR_W / 2 - width / 2)

    for ch in s:
        cp = ord(ch)
        if cp < 0x80:
            first, last, cell, gl = font
            g = gl.get(cp)
            if g:
                for row in range(cell):
                    for col in range(cell):
                        if g[row] & (0x80 >> col):
                            for dy in range(ASCII_SCALE):
                                for dx in range(ASCII_SCALE):
                                    px[x + col * ASCII_SCALE + dx,
                                       y + row * ASCII_SCALE + dy] = colour
        else:
            bits = glyphs.get(cp)
            if bits:
                for row in range(GLYPH):
                    for col in range(GLYPH):
                        if bits[row] & (1 << (GLYPH - 1 - col)):
                            for dy in range(CJK_SCALE):
                                for dx in range(CJK_SCALE):
                                    px[x + col * CJK_SCALE + dx,
                                       y + row * CJK_SCALE + dy] = colour
        x += advance(cp)


def render(page, lines, title, hint, glyphs, font):
    img = Image.new("RGB", (SCR_W, SCR_H), (0x0C, 0x12, 0x0E))
    px = img.load()
    ACC, TEXT, DIM = (0x5F, 0xC4, 0x7A), (0xF2, 0xF2, 0xF6), (0x6E, 0x7E, 0x72)
    draw_utf8(px, glyphs, font, title, TITLE_Y, ACC)
    top = 150 - (len(lines) - 1) * LINE_PITCH // 2
    for i, line in enumerate(lines):
        draw_utf8(px, glyphs, font, line, top + i * LINE_PITCH, TEXT)
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pages", nargs="*", type=int)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "help_pages.png"))
    args = ap.parse_args()

    pages, labels = load_pages()
    glyphs = load_glyphs()
    font = load_font(FONT)
    total = len(pages)
    print(f"{total} pages, {len(glyphs)} glyphs")

    missing = sorted({ord(c) for p in pages for l in p for c in l
                      if ord(c) > 0x7F and ord(c) not in glyphs})
    if missing:
        print("MISSING GLYPHS:", " ".join(f"U+{c:04X}" for c in missing))
        return 1

    pick = list(range(total)) if args.all else (args.pages or [0, 1, 3, 5, 6, 8])
    tiles = [render(i, pages[i], "怎么玩", "点一下 下一页", glyphs, font) for i in pick]
    cols = len(tiles)
    out = Image.new("RGB", (SCR_W * cols, SCR_H + 16), (0x08, 0x08, 0x0C))
    for i, tile in enumerate(tiles):
        out.paste(tile, (i * SCR_W, 16))
        ImageDraw.Draw(out).text((i * SCR_W + 4, 2), f"page {pick[i] + 1}: "
                                 f"{' / '.join(pages[pick[i]])}",
                                 fill=(0xFF, 0xC0, 0x60))
    out.save(args.out)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
