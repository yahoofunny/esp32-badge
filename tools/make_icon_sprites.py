#!/usr/bin/env python3
"""Build the three launcher icons by posing clawd's own body.

Earlier attempts bolted props onto him -- a crate, an idea bulb, headphones, a laptop
-- taken from the animations in clawd-on-desk's assets/gif. At launcher size those are
foreign objects sitting on top of the character, and they looked like it on the panel.
This does what the animations' own "mini" variants do: same creature, same colours,
different pose.

Three edits, each a few pixel runs on the 378x253 source art:

  roll  rotated 14 degrees, so the whole silhouette leans -- he is mid-roll.
  tap   the right arm comes off and is replaced by one standing on the shoulder: the
        reaching-up pose, which is what the game is (he appears and you have to spot
        him).
  pet   two ears on the head and the body squashed a little: a smaller, rounder
        creature, which is what a pet is.

The fractions are measured off the artwork, not estimated: the body column runs
0.159..0.844 of the width, the bar the arms make is 0.253..0.506 of the height, the
legs start at 0.731.

Output: system/assets_icons.h -- 2bpp, two colours (body and ink) plus transparency,
so the icons differ by shape rather than by hue, and the palettes are the art's own.
The palette is four entries because 2bpp addresses four values.

    tools/make_icon_sprites.py
    tools/make_icon_sprites.py --width 96      # try a larger sprite
"""

import argparse
import os

from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "system", "clawd-source.png")
OUT = os.path.join(ROOT, "system", "assets_icons.h")

# Measured from the source art; see the docstring.
BODY_X0, BODY_X1 = 0.1587, 0.8439
ARM_Y0, ARM_Y1 = 0.2530, 0.5060
LEG_Y0 = 0.7312

BODY_RGB = (0xD7, 0x77, 0x57)
INK_RGB = (0x17, 0x12, 0x1C)
ALPHA_MIN = 128
COVERAGE_FLOOR = 0.34
INK_CUTOFF = 100          # mean channel below which a pixel is the ink


def edit_roll(art):
    """Mid-roll: the body leans, as a shear.

    Rotating was tried first and is wrong for this art. At 48 px an arbitrary-angle
    rotation resamples every edge into stair-steps two pixels wide, so the rectangular
    body turns to gravel. A shear moves each row sideways as a whole: no resampling,
    the pixels stay square, and the verticals lean, which is the part that reads as
    leaning. The rim stays level, so it does not look like the sprite is falling over.
    """
    w, h = art.size
    lean = int(0.16 * w)                     # sideways travel, top to bottom
    out = Image.new("RGBA", (w + 2 * lean, h), (0, 0, 0, 0))
    for y in range(h):
        dx = lean + int(lean * (0.5 - y / (h - 1)) * 2)
        out.paste(art.crop((0, y, w, y + 1)), (dx, y))
    return out


def edit_tap(art):
    """One arm raised: a stub standing on the outer end of the right arm.

    Anything drawn clear of the body reads as a second object rather than as a limb,
    so the stub grows out of the arm that is already there and overlaps it.
    """
    w, h = art.size
    body = art.getpixel((w // 2, h // 2))
    out = art.copy()
    arm_w = int(0.078 * w)
    arm_h = int(0.42 * h)
    ImageDraw.Draw(out).rectangle(
        [w - arm_w - int(0.01 * w), int(ARM_Y0 * h) - arm_h,
         w - int(0.01 * w) - 1, int(ARM_Y0 * h) + int((ARM_Y1 - ARM_Y0) * h * 0.45)],
        fill=body)
    return out


def edit_pet(art):
    """Two ears and a rounder body: the same creature, smaller."""
    w, h = art.size
    ears = int(0.20 * h)
    taller = Image.new("RGBA", (w, h + ears), (0, 0, 0, 0))
    taller.paste(art, (0, ears))
    body = art.getpixel((w // 2, h // 2))
    draw = ImageDraw.Draw(taller)
    ear_w = int(0.086 * w)
    for fx in (0.27, 0.665):
        draw.rectangle([int(fx * w), 0, int(fx * w) + ear_w, ears + 4], fill=body)
    # Tuck the arms in as well, so the pose is not just the base art plus ears.
    for x0, x1 in ((0, int(BODY_X0 * w)), (int(BODY_X1 * w), w)):
        draw.rectangle([x0, int(ARM_Y0 * h) + ears, x1, int(ARM_Y1 * h) + ears],
                       fill=(0, 0, 0, 0))
        half = (x1 - x0) // 2
        draw.rectangle([x1 - half, int(ARM_Y0 * h) + ears, x1, int(ARM_Y1 * h) + ears],
                       fill=body)
    return taller.resize((w, h), Image.NEAREST)


ICONS = [
    ("roll", edit_roll),
    ("tap",  edit_tap),
    ("pet",  edit_pet),
]


def classify(rgba):
    """0 for clear, 1 for body, 2 for ink."""
    px = rgba.load()
    w, h = rgba.size
    out = [[0] * w for _ in range(h)]
    for y in range(h):
        row = out[y]
        for x in range(w):
            r, g, b, a = px[x, y]
            if a < ALPHA_MIN:
                continue
            row[x] = 2 if (r + g + b) / 3 < INK_CUTOFF else 1
    return out


def downsample(idx, w, h, out_w, out_h):
    """Majority colour per block, kept when the block is covered by at least
    COVERAGE_FLOOR -- enough to hold on to the legs, which a plain majority drops."""
    out = [[0] * out_w for _ in range(out_h)]
    for oy in range(out_h):
        sy0, sy1 = oy * h // out_h, max(oy * h // out_h + 1, (oy + 1) * h // out_h)
        for ox in range(out_w):
            sx0, sx1 = ox * w // out_w, max(ox * w // out_w + 1, (ox + 1) * w // out_w)
            counts = [0, 0, 0]
            total = 0
            for sy in range(sy0, sy1):
                row = idx[sy]
                for sx in range(sx0, sx1):
                    counts[row[sx]] += 1
                    total += 1
            if total and (total - counts[0]) / total >= COVERAGE_FLOOR:
                out[oy][ox] = 1 if counts[1] >= counts[2] else 2
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


def rgb565(c):
    return ((c[0] & 0xF8) << 8) | ((c[1] & 0xFC) << 3) | (c[2] >> 3)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--width", type=int, default=48,
                    help="sprite width in px (default 48: the carousel draws icons "
                         "26-41 px wide, growing to ~87 while one launches)")
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()

    im = Image.open(SOURCE).convert("RGBA")
    bbox = im.split()[3].getbbox()
    if bbox is None:
        raise SystemExit(f"error: {SOURCE} is fully transparent")
    base = im.crop(bbox)

    built = []
    for symbol, edit in ICONS:
        art = edit(base)
        aw, ah = art.size
        out_w = args.width
        out_h = max(1, round(ah * out_w / aw))
        small = downsample(classify(art), aw, ah, out_w, out_h)
        data = pack(small, out_w, out_h)
        built.append((symbol, out_w, out_h, data))
        print(f"{symbol:4s} {aw}x{ah} -> {out_w}x{out_h}  {len(data)} bytes")

    lines = [
        "// Generated by tools/make_icon_sprites.py -- do not edit by hand.",
        "//",
        "// One launcher icon per app: the same clawd, posed differently. Two colours",
        "// plus transparency, so the three differ by shape rather than by hue. The",
        "// palettes are byte-swapped for the panel already, because gfx's sprite",
        "// drawer writes palette entries straight to the framebuffer.",
        "#pragma once",
        "",
        "#include \"gfx.h\"",
        "",
    ]
    for symbol, w, h, data in built:
        row_bytes = (w + 3) // 4
        lines.append(f"static const uint8_t icon_{symbol}_bits[{len(data)}] = {{")
        for i in range(0, len(data), 16):
            lines.append("    " + " ".join(f"0x{b:02X}," for b in data[i:i + 16]))
        lines += [
            "};",
            f"static const uint16_t icon_{symbol}_palette[4] = {{",
            "    0,                    // transparent, never read",
            f"    PX(0x{rgb565(BODY_RGB):04X}),    // body  "
            f"#{BODY_RGB[0]:02X}{BODY_RGB[1]:02X}{BODY_RGB[2]:02X}",
            f"    PX(0x{rgb565(INK_RGB):04X}),    // ink   "
            f"#{INK_RGB[0]:02X}{INK_RGB[1]:02X}{INK_RGB[2]:02X}",
            "    0,                    // index 3 is never set",
            "};",
            f"static const gfx::Sprite icon_{symbol} = {{ icon_{symbol}_bits, {w}, {h}, "
            f"{row_bytes} }};",
            "",
        ]
    with open(args.out, "w", encoding="utf-8", newline=chr(10)) as fh:
        fh.write("\n".join(lines))
    print(f"wrote {args.out} ({sum(len(b[3]) for b in built)} bytes of bitmap data)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
