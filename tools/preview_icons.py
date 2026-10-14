#!/usr/bin/env python3
"""Render the launcher icons in system/assets_icons.h, on the discs the carousel paints.

The launcher preview deliberately stops at placeholder circles, so the three icons were
never checked as a set. This does check them: downscaling a sprite to the size the ring
actually draws it at is where thin legs and small props disappear, and that is not
visible in the header.

    tools/preview_icons.py            # all three, at the ring's near and far sizes
    tools/preview_icons.py --zoom 4
"""

import argparse
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

# Must match the accents in the games' headers.
DISCS = {
    "roll": (0xE8, 0x3B, 0x3B),
    "tap":  (0x2E, 0xC4, 0x6B),
    "pet":  (0xD9, 0x77, 0x57),
}
# What launcher.cpp passes to drawIcon is size * 0.56, for a ring size of 46..74 --
# plus the boost while an entry launches.
ICON_SIZES = [0.56 * 46, 0.56 * 58, 0.56 * 74, 0.56 * 74 * 2.1]


def swap16(c):
    return ((c & 0x00FF) << 8) | ((c & 0xFF00) >> 8)


def load(path):
    src = open(path, encoding="utf-8").read()
    out = {}
    for sym in DISCS:
        m = re.search(rf"icon_{sym}_bits\[(\d+)\] = \{{(.*?)\}};", src, re.S)
        if not m:
            raise SystemExit(f"preview: icon_{sym} not in {path}")
        data = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(2)))
        t = re.search(rf"icon_{sym} = \{{ icon_{sym}_bits, (\d+), (\d+), (\d+) \}}", src)
        w, h, rb = (int(t.group(i)) for i in (1, 2, 3))
        raw = [int(x, 16) for x in re.findall(
            r"PX\(0x([0-9A-F]{4})\)",
            re.search(rf"icon_{sym}_palette\[4\] = \{{(.*?)\}};", src, re.S).group(1))]
        pal = [(0, 0, 0)]
        for literal in raw:
            # The whole chain, so a mistake in the generator shows up here: the
            # firmware stores PX(literal) in the palette, writes that value to the
            # framebuffer untouched, and the panel reads the framebuffer big-endian.
            # Getting this model wrong is how the first icon set shipped with its
            # colours byte-swapped while this preview showed them correctly.
            in_framebuffer = swap16(literal)             # PX(literal)
            on_panel = swap16(in_framebuffer)            # the panel reads big-endian
            pal.append((((on_panel >> 11) & 0x1F) * 255 // 31,
                        ((on_panel >> 5) & 0x3F) * 255 // 63,
                        (on_panel & 0x1F) * 255 // 31))
        out[sym] = (w, h, rb, data, pal)
    return out


def draw(px, bounds, size, sprite, disc, icon_w, cx, cy):
    """The launcher's disc, then the sprite nearest-neighbour at icon_w wide.

    Nearest-neighbour is what gfx does, and it is the step worth checking: at the far
    side of the ring the icon is drawn at 26 px from a 48 px sprite, and whatever the
    sampler drops there is dropped on the device too.
    """
    w, h, rb, data, pal = sprite
    bw, bh = bounds

    def put(x, y, col):
        if 0 <= x < bw and 0 <= y < bh:
            px[x, y] = col

    dim = tuple(int(c * 0.70) for c in disc)
    r = size / 2
    for yy in range(max(0, int(cy - r) - 1), min(bh, int(cy + r) + 2)):
        for xx in range(max(0, int(cx - r) - 1), min(bw, int(cx + r) + 2)):
            if (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r:
                put(xx, yy, dim)
    scale = icon_w / w
    out_h = round(h * scale)
    left, top = int(cx - icon_w / 2), int(cy - out_h / 2)
    for oy in range(out_h):
        sy = min(h - 1, int(oy / scale))
        for ox in range(int(icon_w)):
            sx = min(w - 1, int(ox / scale))
            v = (data[sy * rb + (sx >> 2)] >> ((sx & 3) * 2)) & 3
            if v:
                put(left + ox, top + oy, pal[v])


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--header", default=os.path.join(ROOT, "system", "assets_icons.h"))
    ap.add_argument("--zoom", type=int, default=3)
    ap.add_argument("--out", default=os.path.join(ROOT, "build/preview/icons_on_disc.png"))
    args = ap.parse_args()

    sprites = load(args.header)
    # Room for the launch boost, which draws the disc at 74 * 2.1 as well.
    cell = int(74 * 2.1) + 8
    out = Image.new("RGB", (cell * len(ICON_SIZES), cell * len(sprites)), (0x0E, 0x0E, 0x16))
    for r, sym in enumerate(sprites):
        for c, icon_w in enumerate(ICON_SIZES):
            tile = Image.new("RGB", (cell, cell), (0x0E, 0x0E, 0x16))
            # The launcher grows the disc and the icon together, so recovering the
            # ring size from the icon width keeps the two in the right proportion.
            draw(tile.load(), tile.size, icon_w / 0.56, sprites[sym], DISCS[sym],
                 icon_w, cell / 2, cell / 2)
            out.paste(tile, (c * cell, r * cell))
        print(f"  {sym}: sprite {sprites[sym][0]}x{sprites[sym][1]}, drawn at "
              + ", ".join(f"{s:.0f}" for s in ICON_SIZES) + " px wide")
    out = out.resize((out.width * args.zoom, out.height * args.zoom), Image.NEAREST)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    out.save(args.out)
    print(f"wrote {args.out}  rows: {', '.join(sprites)}, columns: icon widths above, "
          f"{args.zoom}x")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
