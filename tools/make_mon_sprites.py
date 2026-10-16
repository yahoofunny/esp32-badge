#!/usr/bin/env python3
"""Build the pocket-mon forms from the clawd artwork.

The mon line borrows clawd's silhouette rather than drawing new creatures: what makes a
form recognisable here is its size, its colour, and the mark the firmware draws on top of
it -- which is the same trick the pet uses to cover seven moods with four bodies.

Two things are done to the source before it is emitted:

  * union downsampling, copied from make_pet_sprites.py. Majority voting erodes the four
    legs down to nothing at baby size; taking the union keeps every leg at least a pixel.

  * a one-pixel outline is folded in as index 2. The source is a flat shape, and a flat
    shape in one colour on a dark background reads as a blob; an outline is what makes the
    same bitmap look like a drawn creature. It is computed on the *output* grid, because an
    outline drawn on the 400 px source would be eroded to nothing by the downsampling.

Index 1 is the body, 2 the outline, 3 is left for the firmware to use as it likes and 0 is
transparent. The eyes are punched out of the source and their geometry is recorded per
form, so game_mon.cpp can redraw them every frame and cover every mood with one bitmap.

Usage:
    tools/make_mon_sprites.py
    tools/make_mon_sprites.py --source system/clawd-source.png --output system/assets_mon.h
"""

import argparse
import pathlib
import sys
from collections import deque

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")

DEFAULT_SOURCE = "system/clawd-source.png"
DEFAULT_OUTPUT = "system/assets_mon.h"

# name, stage, WxH, body RRGGBB
#
# Sizes step with the stage rather than with the form, so the two child forms are the same
# size and the branch reads as a colour and a mark rather than as a growth jump. The adult
# DUSK form is a size smaller on purpose: neglect should look stunted.
FORMS = [
    ("pip",   "BABY",  "72x48",   "C9B6E4"),
    ("leaf",  "CHILD", "104x69",  "6FBF6A"),
    ("aqua",  "CHILD", "104x69",  "5FA8E0"),
    ("bloom", "ADULT", "148x99",  "8FD06A"),
    ("tide",  "ADULT", "148x99",  "4FC3D9"),
    ("solar", "ADULT", "148x99",  "F2C14E"),
    ("dusk",  "ADULT", "132x88",  "7A7A92"),
]

OUTLINE_SCALE = 0.42   # the outline is the body colour taken this far towards black


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

    opaque, eye = [], []
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
    """Centre, half-extent and half-separation of the eye pair, averaged for symmetry."""
    eyes = []
    for x0, y0, x1, y1, _ in blobs:
        eyes.append(((x0 + x1) / 2.0, (y0 + y1) / 2.0, max(x1 - x0, y1 - y0) / 2.0))
    ax = (eyes[0][0] + eyes[1][0]) / 2.0
    ay = (eyes[0][1] + eyes[1][1]) / 2.0
    ar = (eyes[0][2] + eyes[1][2]) / 2.0
    dx = abs(eyes[1][0] - eyes[0][0]) / 2.0
    return (ax, ay, ar, dx)


def downsample(opaque, w, h, out_w, out_h):
    """Union-downsample to out_w x out_h; 1 is body, 0 is empty."""
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


def add_outline(idx, out_w, out_h):
    """Mark the silhouette's edge as index 2, on the output grid."""
    edge = bytearray(out_w * out_h)
    for y in range(out_h):
        for x in range(out_w):
            p = y * out_w + x
            if idx[p] != 1:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if not (0 <= nx < out_w and 0 <= ny < out_h) or idx[ny * out_w + nx] == 0:
                    edge[p] = 1
                    break
    for p in range(out_w * out_h):
        if edge[p]:
            idx[p] = 2
    return sum(1 for v in edge if v)


def pack(idx, w, h):
    row_bytes = (w + 3) // 4
    data = bytearray(row_bytes * h)
    for y in range(h):
        for x in range(w):
            v = idx[y * w + x]
            if v:
                data[y * row_bytes + (x >> 2)] |= v << ((x & 3) * 2)
    return data


def shade(rgb, factor):
    return tuple(min(255, int(c * factor + 0.5)) for c in rgb)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", default=DEFAULT_SOURCE)
    ap.add_argument("--output", default=DEFAULT_OUTPUT)
    ap.add_argument("--dark-threshold", type=int, default=100)
    ap.add_argument("--alpha-threshold", type=int, default=128)
    args = ap.parse_args()

    root = pathlib.Path(__file__).resolve().parent.parent
    source = root / args.source
    output = root / args.output

    opaque, eye, w, h = load_masks(source, args.dark_threshold, args.alpha_threshold)
    blobs = eye_blobs(eye, w, h)
    ex, ey, er, edx = eye_geometry(blobs)
    print(f"source: {w}x{h}, eyes at ({ex:.1f},{ey:.1f}) r={er:.1f} dx={edx:.1f}")

    out = []
    for name, stage, spec, body_hex in FORMS:
        out_w, out_h = (int(v) for v in spec.lower().split("x"))
        idx = downsample(opaque, w, h, out_w, out_h)
        edge_px = add_outline(idx, out_w, out_h)
        data = pack(idx, out_w, out_h)
        sx = out_w / w
        e_r = max(3, int(round(er * sx)))
        e_dx = int(round(edx * sx))
        e_dy = int(round((ey - h / 2.0) * (out_h / h)))
        body = tuple(int(body_hex[i:i + 2], 16) for i in (0, 2, 4))
        edge = shade(body, OUTLINE_SCALE)
        ink = shade(body, 0.18)
        out.append((name, stage, body_hex.upper(), out_w, out_h, (out_w + 3) // 4, data,
                    e_dx, e_dy, e_r, edge_px,
                    rgb888_to_rgb565(*body), rgb888_to_rgb565(*edge), rgb888_to_rgb565(*ink)))
        print(f"  {name:<6} {stage:<6} {out_w:3d}x{out_h:<3d} {len(data):5d} B  "
              f"edge {edge_px:4d} px  eye dx={e_dx} dy={e_dy} r={e_r}")

    lines = [
        "// Generated by tools/make_mon_sprites.py -- do not edit by hand.",
        "//",
        "// The pocket-mon forms: clawd's silhouette at one size per stage, outlined, with",
        "// the artwork's eyes punched out and their geometry recorded so the firmware can",
        "// draw them and cover every mood with one bitmap. Forms in the same stage share a",
        "// size, so a branch shows up as a colour and a mark rather than as a growth jump.",
        "//",
        "// 2bpp: 0 transparent, 1 body, 2 outline, 3 spare.",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "typedef struct {",
        "    const uint8_t *bits;",
        "    uint8_t w, h, rowBytes;",
        "    int8_t  eyeDX, eyeDY;   // eye centre, from the sprite's centre",
        "    uint8_t eyeR;           // radius of the hole left in the art",
        "    uint16_t body;          // panel-visible RGB565; the renderer byte-swaps",
        "    uint16_t edge;",
        "    uint16_t ink;           // the drawn eyes",
        "    const char *name;",
        "    const char *stage;",
        "} mon_form_t;",
        "",
        f"#define MON_FORM_COUNT {len(out)}",
    ]
    for rec in out:
        name = rec[0]
        lines.append("")
        lines.append(f"static const uint8_t mon_bits_{name}[{len(rec[6])}] = {{")
        for i in range(0, len(rec[6]), 16):
            lines.append("    " + " ".join(f"0x{b:02X}," for b in rec[6][i:i + 16]))
        lines.append("};")
    lines.append("")
    lines.append("static const mon_form_t mon_forms[MON_FORM_COUNT] = {")
    for (name, stage, body_hex, out_w, out_h, row_bytes, _data, e_dx, e_dy, e_r,
         _edge, body565, edge565, ink565) in out:
        lines.append(f"    {{ mon_bits_{name}, {out_w}, {out_h}, {row_bytes}, "
                     f"{e_dx}, {e_dy}, {e_r}, 0x{body565:04X}, 0x{edge565:04X}, "
                     f"0x{ink565:04X}, \"{name.upper()}\", \"{stage}\" }},   // #{body_hex}")
    lines.append("};")
    lines.append("")

    output.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"wrote {output.relative_to(root)} "
          f"({sum(len(r[6]) for r in out)} bytes of bitmap data)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
