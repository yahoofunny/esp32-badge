#!/usr/bin/env python3
"""Generate the clawd-roll icon.

Drawn from the same primitives and palette as the game itself, so the icon cannot
drift out of sync with what actually appears on the screen: same background, same
arena ring, same clawd sprite, same red body colour.

    tools/make_icon.py [output.png] [--size 512] [--body RRGGBB]

Writes a square PNG. Nothing here is used at runtime -- the game draws its own
backdrop procedurally; this is for the repository, the README and store-style
presentation.
"""

import argparse
from PIL import Image, ImageDraw

# Kept in step with the constants in games/clawd-roll/clawd-roll.ino
COL_BG     = (0x0B, 0x0B, 0x12)
COL_RING   = (0x5A, 0x5A, 0xC8)
COL_GLOW   = (0x24, 0x24, 0x52)
COL_DOT    = (0x1A, 0x1A, 0x2E)
COL_ACCENT = (0xFF, 0xD2, 0x3E)

DEFAULT_BODY = (0xE8, 0x3B, 0x3B)
DETAIL       = (0x14, 0x0D, 0x0D)


def hex_to_rgb(s: str):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def load_clawd(path: str):
    im = Image.open(path).convert("RGBA")
    return im.crop(im.split()[3].getbbox())


def build(size: int, clawd_path: str, body: tuple) -> Image.Image:
    S = size
    img = Image.new("RGBA", (S, S), COL_BG + (255,))
    d = ImageDraw.Draw(img)

    c = S / 2.0
    # Arena wall. These radii are the game's own proportions scaled up:
    # arena 172 of 360, wall band ~2px, inner glow ~5px.
    r_wall = 172.0 / 360.0 * S
    band   = max(2, round(2.5 / 360.0 * S))
    glow   = max(3, round(6.0 / 360.0 * S))

    # inner glow, drawn as a few concentric strokes so it reads as a soft falloff
    for i in range(glow):
        t = i / max(1, glow - 1)
        col = tuple(int(COL_BG[j] + (COL_GLOW[j] - COL_BG[j]) * t) for j in range(3))
        rr = r_wall - band - i
        d.ellipse((c - rr, c - rr, c + rr, c + rr), outline=col + (255,), width=1)

    # the wall itself
    d.ellipse((c - r_wall, c - r_wall, c + r_wall, c + r_wall),
              outline=COL_RING + (255,), width=band)

    # floor dots, same 45px lattice and offset as the game backdrop
    step = 45.0 / 360.0 * S
    dot_r = max(1, round(1.6 / 360.0 * S))
    x = 22.0 / 360.0 * S
    while x < S:
        y = 22.0 / 360.0 * S
        while y < S:
            if ((x - c) ** 2 + (y - c) ** 2) ** 0.5 < r_wall - band - glow:
                d.ellipse((x - dot_r, y - dot_r, x + dot_r, y + dot_r), fill=COL_DOT + (255,))
            y += step
        x += step

    # clawd, tilted so the pose reads as mid-roll rather than parked
    clawd = load_clawd(clawd_path)

    # recolour: the source sprite is flat orange on transparent with dark details
    px = clawd.load()
    for yy in range(clawd.height):
        for xx in range(clawd.width):
            r, g, b, a = px[xx, yy]
            if a < 128:
                px[xx, yy] = (0, 0, 0, 0)
            elif (r + g + b) / 3 < 100:
                px[xx, yy] = DETAIL + (255,)
            else:
                px[xx, yy] = body + (255,)

    target_w = int(S * 0.575)
    sc = target_w / clawd.width
    clawd = clawd.resize((target_w, max(1, int(clawd.height * sc))), Image.LANCZOS)
    clawd = clawd.rotate(-16, resample=Image.BICUBIC, expand=True)

    # shift the sprite slightly so the composition is not dead-centre
    cw, ch = clawd.size
    img.paste(clawd, (int(c - cw / 2), int(c - ch / 2 - S * 0.015)), clawd)

    # contact shadow under clawd, drawn before it would be better but a soft
    # ellipse behind the feet reads fine over the dark floor
    return img


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output", nargs="?", default="games/clawd-roll/icon.png")
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--body", default=None, help="body colour as RRGGBB (default: the game's red)")
    ap.add_argument("--clawd", default="games/clawd-roll/clawd-source.png")
    ap.add_argument("--flatten", action="store_true",
                    help="composite onto the dark background instead of keeping alpha")
    args = ap.parse_args()

    body = hex_to_rgb(args.body) if args.body else DEFAULT_BODY
    img = build(args.size, args.clawd, body)

    if args.flatten:
        flat = Image.new("RGB", img.size, COL_BG)
        flat.paste(img, (0, 0), img)
        img = flat

    img.save(args.output)
    print(f"wrote {args.output} ({img.width}x{img.height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
