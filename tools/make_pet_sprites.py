#!/usr/bin/env python3
"""Build the pet's body sprites from the clawd artwork.

The pet is clawd -- the same character the rolling game uses -- rather than a
cartoon face, and it grows through four sizes as it ages. Two things make that
work at pet scale:

  * the art is downsampled with an "any opaque source pixel wins" rule rather
    than a majority vote. Majority voting erodes clawd's four legs, which are
    only about 7 px wide in the 378 px original, until they disappear at the
    smallest size. Taking the union keeps every leg at least one pixel wide.

  * the eyes are punched out. In the original they are the only dark pixels, so
    they are easy to find, and blanking them to the body colour leaves a clean
    head for the firmware to draw its own eyes into. That is what lets one
    sprite express every mood, instead of needing a bitmap per emotion.

Output: system/assets_pet.h -- one 2bpp sprite per stage, plus the eye centre and
radius in that stage's pixels so the drawn eyes land where the art had them.

Usage:
    tools/make_pet_sprites.py                       # default source and sizes
    tools/make_pet_sprites.py --source art.png --sizes 80x54,120x80
"""

import argparse
import sys
from collections import deque

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")


# Widths chosen so each stage is a clear step up from the last, and so the adult
# still leaves the right-hand third of the screen free for the pet's speech.
DEFAULT_SIZES = ["88x59", "108x72", "128x86", "148x99"]
STAGE_NAMES = ["baby", "child", "teen", "adult"]
DEFAULT_SOURCE = "system/clawd-source.png"
DEFAULT_OUTPUT = "system/assets_pet.h"


def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def load_masks(path, dark_threshold, alpha_threshold):
    """Return (opaque, eye) as row-major lists of bool, plus the cropped size."""
    im = Image.open(path).convert("RGBA")
    bbox = im.split()[3].getbbox()
    if bbox is None:
        raise SystemExit(f"error: {path} is fully transparent")
    im = im.crop(bbox)
    w, h = im.size

    rgb = im.convert("RGB").load()
    alpha = im.split()[3].load()

    opaque = []
    eye = []
    for y in range(h):
        for x in range(w):
            if alpha[x, y] < alpha_threshold:
                opaque.append(False)
                eye.append(False)
            else:
                r, g, b = rgb[x, y]
                opaque.append(True)
                eye.append((r + g + b) / 3 < dark_threshold)
    return opaque, eye, w, h


def eye_blobs(eye, w, h, min_pixels=40):
    """Bounding boxes of the connected dark regions, biggest two first."""
    seen = [False] * (w * h)
    boxes = []
    for start in range(w * h):
        if not eye[start] or seen[start]:
            continue
        q = deque([start])
        seen[start] = True
        x0 = x1 = start % w
        y0 = y1 = start // w
        count = 0
        while q:
            p = q.popleft()
            count += 1
            px, py = p % w, p // w
            x0, x1 = min(x0, px), max(x1, px)
            y0, y1 = min(y0, py), max(y1, py)
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = px + dx, py + dy
                if 0 <= nx < w and 0 <= ny < h:
                    n = ny * w + nx
                    if eye[n] and not seen[n]:
                        seen[n] = True
                        q.append(n)
        if count >= min_pixels:
            boxes.append((x0, y0, x1, y1, count))
    boxes.sort(key=lambda b: -b[4])
    if len(boxes) < 2:
        raise SystemExit(f"error: found {len(boxes)} eye blob(s), expected two")
    return boxes[:2]


def eye_geometry(blobs):
    """Centre and half-extent of the eye pair, averaged for symmetry.

    The eyes in the original are squares, so the half-extent is the half-side,
    which is what a drawn eye should be sized to: it fills the footprint the
    artwork left for it.
    """
    eyes = []
    for x0, y0, x1, y1, _ in blobs:
        eyes.append(((x0 + x1) / 2.0, (y0 + y1) / 2.0, max(x1 - x0, y1 - y0) / 2.0))
    ax = (eyes[0][0] + eyes[1][0]) / 2.0
    ay = (eyes[0][1] + eyes[1][1]) / 2.0
    ar = (eyes[0][2] + eyes[1][2]) / 2.0
    dx = abs(eyes[1][0] - eyes[0][0]) / 2.0
    return (ax, ay, ar, dx)


def downsample(opaque, w, h, out_w, out_h):
    """Union-downsample to out_w x out_h.

    Every opaque source pixel counts as body, the eyes included: the art's eyes
    are meant to be covered, not cut out. Leaving them as holes would show the
    background through the pet's head around whatever the firmware draws.
    """
    idx = bytearray(out_w * out_h)
    for oy in range(out_h):
        sy0 = oy * h // out_h
        sy1 = max(sy0 + 1, (oy + 1) * h // out_h)
        for ox in range(out_w):
            sx0 = ox * w // out_w
            sx1 = max(sx0 + 1, (ox + 1) * w // out_w)
            hit = False
            for sy in range(sy0, sy1):
                base = sy * w
                for sx in range(sx0, sx1):
                    if opaque[base + sx]:
                        hit = True
                        break
                if hit:
                    break
            idx[oy * out_w + ox] = 1 if hit else 0
    return idx


def pack(idx, w, h):
    row_bytes = (w + 3) // 4
    data = bytearray(row_bytes * h)
    for y in range(h):
        for x in range(w):
            v = idx[y * w + x]
            if v:
                data[y * row_bytes + (x >> 2)] |= v << ((x & 3) * 2)
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default=DEFAULT_SOURCE)
    ap.add_argument("--output", default=DEFAULT_OUTPUT)
    ap.add_argument("--sizes", default=",".join(DEFAULT_SIZES),
                    help=f"comma-separated WxH per stage (default {','.join(DEFAULT_SIZES)})")
    ap.add_argument("--body", default="D77757", help="body colour RRGGBB")
    ap.add_argument("--detail", default="17121C", help="eye colour RRGGBB")
    ap.add_argument("--dark-threshold", type=int, default=100)
    ap.add_argument("--alpha-threshold", type=int, default=128)
    args = ap.parse_args()

    sizes = []
    for spec in args.sizes.split(","):
        w, h = spec.lower().split("x")
        sizes.append((int(w), int(h)))
    if len(sizes) != len(STAGE_NAMES):
        raise SystemExit(f"error: need {len(STAGE_NAMES)} sizes, got {len(sizes)}")

    opaque, eye, w, h = load_masks(args.source, args.dark_threshold, args.alpha_threshold)
    blobs = eye_blobs(eye, w, h)
    ex, ey, er, edx = eye_geometry(blobs)
    print(f"source: {w}x{h}, eyes at ({ex:.1f},{ey:.1f}) r={er:.1f} dx={edx:.1f} "
          f"({blobs[0][4]} + {blobs[1][4]} px)")

    body = tuple(int(args.body[i:i + 2], 16) for i in (0, 2, 4))
    detail = tuple(int(args.detail[i:i + 2], 16) for i in (0, 2, 4))

    sprites = []
    for (out_w, out_h), name in zip(sizes, STAGE_NAMES):
        idx = downsample(opaque, w, h, out_w, out_h)
        data = pack(idx, out_w, out_h)
        sx = out_w / w
        # The drawn eye takes the footprint the artwork left for it.
        e_r = max(3, int(round(er * sx)))
        e_dx = int(round(edx * sx))
        e_dy = int(round((ey - h / 2.0) * (out_h / h)))
        sprites.append((name, out_w, out_h, (out_w + 3) // 4, data, e_dx, e_dy, e_r))
        print(f"  {name:<6} {out_w:3d}x{out_h:<3d} {len(data):5d} B  "
              f"eye dx={e_dx} dy={e_dy} r={e_r}")

    lines = [
        "// Generated by tools/make_pet_sprites.py -- do not edit by hand.",
        "//",
        "// The pet's body: clawd himself, one sprite per life stage, with the eyes",
        "// that were in the artwork punched out. The firmware draws the eyes, at the",
        "// centre recorded in each entry, so a single body covers every mood.",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define PET_PALETTE_COUNT 3",
        "// Palette in panel-visible RGB565; the renderer byte-swaps, this does not.",
        f"static const uint16_t pet_palette[PET_PALETTE_COUNT] = {{",
        "    0x0000,                  // transparent",
        f"    0x{rgb888_to_rgb565(*body):04X},                  // body",
        f"    0x{rgb888_to_rgb565(*detail):04X},                  // detail (unused by the",
        "                             // body, kept so both sprites share a palette)",
        "};",
        "",
        "typedef struct {",
        "    const uint8_t *bits;   // 2bpp, row-major, LSB-first; 0 is transparent",
        "    uint8_t w, h, rowBytes;",
        "    int8_t  eyeDX, eyeDY;  // eye centre, from the sprite's centre",
        "    uint8_t eyeR;          // radius of the hole left in the art",
        "} pet_sprite_t;",
        "",
        f"#define PET_STAGE_COUNT {len(sprites)}",
    ]
    for name, out_w, out_h, row_bytes, data, e_dx, e_dy, e_r in sprites:
        lines.append("")
        lines.append(f"static const uint8_t pet_bits_{name}[{len(data)}] = {{")
        for i in range(0, len(data), 16):
            lines.append("    " + " ".join(f"0x{b:02X}," for b in data[i:i + 16]))
        lines.append("};")
    lines.append("")
    lines.append("static const pet_sprite_t pet_sprites[PET_STAGE_COUNT] = {")
    for name, out_w, out_h, row_bytes, data, e_dx, e_dy, e_r in sprites:
        lines.append(f"    {{ pet_bits_{name}, {out_w}, {out_h}, {row_bytes}, "
                     f"{e_dx}, {e_dy}, {e_r} }},   // {name}")
    lines.append("};")
    lines.append("")

    with open(args.output, "w", encoding="utf-8", newline=chr(10)) as fh:
        fh.write("\n".join(lines))
    print(f"wrote {args.output} "
          f"({sum(len(s[4]) for s in sprites)} bytes of bitmap data)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
