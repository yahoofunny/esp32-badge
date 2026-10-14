#!/usr/bin/env python3
"""Build one launcher icon per app from clawd's animation frames.

All three games used the same standing clawd, so the carousel showed the same
silhouette three times and the disc colour behind it was the only tell. The
animations in clawd-on-desk's assets/gif are the same character doing different
things, each with a prop that carries at launcher size: a box, an exclamation mark,
headphones. This picks one frame from each and reduces it to a small sprite.

Two things decide whether an icon survives the trip:

  * which frame. The frames of an animation are mostly the same pose, so the useful
    one is the most extended -- the largest ink bounding box over the whole run,
    chosen automatically rather than by eye.

  * how the downsample votes. At 300 px down to 48, a source block is 6x6 and the
    legs are about one destination pixel wide. Taking the union of the block keeps
    them but fattens the whole character; a plain majority loses them. This uses
    majority colour with a coverage floor, which keeps a leg that only covers a
    third of its block.

Output: system/assets_icons.h -- 2bpp sprites, 3 colours each, one palette per icon
because the props differ in hue. Palettes are emitted byte-swapped for the panel,
since gfx's sprite drawer writes them to the framebuffer untouched.

    tools/make_icon_sprites.py             # write the header
    tools/make_icon_sprites.py --width 96  # try a larger sprite
"""

import argparse
import os
import sys

from PIL import Image, ImageSequence

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GIF_DIR = os.path.join(ROOT, "reference", "clawd-gifs")
OUT = os.path.join(ROOT, "system", "assets_icons.h")

# symbol, animation, and the three colours that art is reduced to: the body, the
# prop that makes it recognisable, and the ink (outlines, eyes).
#
# Chosen by rendering every candidate at the size the ring actually draws it and
# looking. Two properties turned out to matter more than the animation's own merits:
#
#   * the prop has to be a compact shape that stands clear of the body. clawd
#     carrying a crate looked dynamic at full size and turned into a smear of blue
#     across his back at 41 px.
#   * the prop's colour has to be its own. Juggling's balls are the perfect prop for
#     a physics game and there is no room for them: body, ink and two ball colours
#     is four, and gfx::Sprite carries three.
#
# So: a dark slab, a yellow bulb, blue headphones. Three distinct silhouettes in
# three distinct colours, each legible down to the far side of the ring.
ICONS = [
    ("roll", "typing",            (0xD8, 0x78, 0x60), (0x4C, 0x61, 0x70), (0x18, 0x18, 0x18)),
    ("tap",  "notification",      (0xD8, 0x78, 0x60), (0xF0, 0xC0, 0x00), (0x00, 0x00, 0x00)),
    ("pet",  "headphones-groove", (0xD8, 0x78, 0x60), (0x18, 0x48, 0x78), (0x00, 0x00, 0x00)),
]

ALPHA_MIN = 128      # below this a source pixel is background, not a dark edge
COVERAGE_FLOOR = 0.34


def load_best_frame(path):
    """The frame with the largest ink bounding box, composited to RGBA."""
    im = Image.open(path)
    best = None
    for i, frame in enumerate(ImageSequence.Iterator(im)):
        rgba = frame.convert("RGBA")
        bbox = rgba.split()[3].point(lambda a: 255 if a >= ALPHA_MIN else 0).getbbox()
        if bbox is None:
            continue
        area = (bbox[2] - bbox[0]) * (bbox[3] - bbox[1])
        if best is None or area > best[0]:
            best = (area, i, rgba.copy(), bbox)
    if best is None:
        raise SystemExit(f"error: {path} has no opaque frame")
    _, index, rgba, bbox = best
    return rgba.crop(bbox), index, im.n_frames


def classify(rgba, palette):
    """Map each pixel to 0 (clear) or 1..3 (nearest of the three colours)."""
    px = rgba.load()
    w, h = rgba.size
    out = [[0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < ALPHA_MIN:
                continue
            best, bestd = 1, None
            for i, (pr, pg, pb) in enumerate(palette, start=1):
                d = (r - pr) ** 2 + (g - pg) ** 2 + (b - pb) ** 2
                if bestd is None or d < bestd:
                    best, bestd = i, d
            out[y][x] = best
    return out


def downsample(idx, w, h, out_w, out_h):
    """Majority colour per block, kept if the block is covered by at least
    COVERAGE_FLOOR -- enough to hold on to legs that a plain majority would drop."""
    out = [[0] * out_w for _ in range(out_h)]
    for oy in range(out_h):
        sy0, sy1 = oy * h // out_h, max(oy * h // out_h + 1, (oy + 1) * h // out_h)
        for ox in range(out_w):
            sx0, sx1 = ox * w // out_w, max(ox * w // out_w + 1, (ox + 1) * w // out_w)
            counts = [0, 0, 0, 0]
            total = 0
            for sy in range(sy0, sy1):
                row = idx[sy]
                for sx in range(sx0, sx1):
                    counts[row[sx]] += 1
                    total += 1
            ink = total - counts[0]
            if total and ink / total >= COVERAGE_FLOOR:
                out[oy][ox] = max(range(1, 4), key=lambda i: counts[i])
    return out


def pack(idx, w, h):
    row_bytes = (w + 3) // 4
    data = bytearray(row_bytes * h)
    for y in range(h):
        for x in range(w):
            v = idx[y][x]
            if v:
                data[y * row_bytes + (x >> 2)] |= v << ((x & 3) * 2)
    return data


def swap16(c):
    return ((c & 0x00FF) << 8) | ((c & 0xFF00) >> 8)


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--width", type=int, default=48,
                    help="sprite width in px (default 48: the carousel draws icons "
                         "26-41 px wide, growing to ~87 while one launches)")
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()

    built = []
    for symbol, anim, body, prop, dark in ICONS:
        path = os.path.join(GIF_DIR, f"clawd-{anim}.gif")
        if not os.path.exists(path):
            raise SystemExit(f"error: {path} missing; see the docstring for the source")
        art, frame, total = load_best_frame(path)
        aw, ah = art.size
        out_w = args.width
        out_h = max(1, round(ah * out_w / aw))
        idx = classify(art, [body, prop, dark])
        small = downsample(idx, aw, ah, out_w, out_h)
        data = pack(small, out_w, out_h)
        built.append((symbol, anim, frame, total, out_w, out_h, data,
                      [body, prop, dark]))
        print(f"{symbol:4s} <- clawd-{anim:18s} frame {frame:3d}/{total:<3d} "
              f"{aw}x{ah} -> {out_w}x{out_h}  {len(data)} bytes")

    lines = [
        "// Generated by tools/make_icon_sprites.py -- do not edit by hand.",
        "//",
        "// One launcher icon per app, each taken from a different clawd animation so",
        "// the carousel's three entries stop being the same silhouette. 2bpp, three",
        "// colours plus transparency. The palettes are byte-swapped for the panel",
        "// already: gfx's sprite drawer writes palette entries to the framebuffer",
        "// untouched, so the swap has to have happened by the time it gets them.",
        "#pragma once",
        "",
        "#include \"gfx.h\"",
        "",
    ]
    for symbol, anim, frame, total, w, h, data, pal in built:
        row_bytes = (w + 3) // 4
        lines += [
            f"// clawd-{anim}.gif, frame {frame} of {total} (the most extended pose).",
            f"static const uint8_t icon_{symbol}_bits[{len(data)}] = {{",
        ]
        for i in range(0, len(data), 16):
            lines.append("    " + " ".join(f"0x{b:02X}," for b in data[i:i + 16]))
        lines += [
            "};",
            f"// body, prop, ink -- pose colours, not the app's accent.",
            f"static const uint16_t icon_{symbol}_palette[4] = {{",
            "    0,   // transparent, never read",
            f"    PX(0x{swap16(rgb565(*pal[0])):04X}),   // #{pal[0][0]:02X}{pal[0][1]:02X}{pal[0][2]:02X}",
            f"    PX(0x{swap16(rgb565(*pal[1])):04X}),   // #{pal[1][0]:02X}{pal[1][1]:02X}{pal[1][2]:02X}",
            f"    PX(0x{swap16(rgb565(*pal[2])):04X}),   // #{pal[2][0]:02X}{pal[2][1]:02X}{pal[2][2]:02X}",
            "};",
            f"static const gfx::Sprite icon_{symbol} = {{ icon_{symbol}_bits, {w}, {h}, "
            f"{row_bytes} }};",
            "",
        ]
    # The ink colour is worth having as a constant: an icon that has to sit on a
    # coloured disc needs a matching outline or it floats.
    lines += [
        "// The sprite's own ink, for anything that needs to draw next to an icon.",
        f"static const uint16_t ICON_INK = PX(0x{swap16(rgb565(*ICONS[0][4])):04X});",
        "",
    ]
    with open(args.out, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    print(f"wrote {args.out} ({sum(len(b[6]) for b in built)} bytes of bitmap data)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
