#!/usr/bin/env python3
"""Render the settings screen, reading its layout out of the firmware.

Unlike the other previews this one parses the constants rather than mirroring them:
the screen is new and has no second copy to keep in step, and the numbers that matter
are exactly the ones written in the C++.

    tools/preview_settings.py --volume 45
"""

import argparse
import os
import re
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from previewlib import (SCR_W, SCR_H, load_font, draw_text, text_centered,  # noqa: E402
                        text_width, apply_bezel, finish)

SRC = os.path.join(ROOT, "system", "game_settings.cpp")


def constants(path):
    src = open(path, encoding="utf-8").read()
    out = {}
    for m in re.finditer(r"static const int (\w+)\s*=\s*(-?\d+)", src):
        out[m.group(1)] = int(m.group(2))
    for m in re.finditer(r"static const float (\w+)\s*=\s*(-?[0-9.]+)f", src):
        out[m.group(1)] = float(m.group(2))
    for pair in re.finditer(r"static const int (\w+)\s*=\s*(-?\d+),\s*(\w+)\s*=\s*(-?\d+)", src):
        out[pair.group(1)] = int(pair.group(2))
        out[pair.group(3)] = int(pair.group(4))
    for m in re.finditer(r"static const uint16_t (\w+)\s*=\s*PX\(RGB565\((0x[0-9A-Fa-f]+), "
                         r"(0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+)\)\)", src):
        out[m.group(1)] = tuple(int(m.group(i), 16) for i in (2, 3, 4))
    # Every label, read out of the firmware rather than copied: the hint line under the
    # -/+ buttons was already stale here once, which is the whole reason this preview
    # parses instead of mirroring.
    for name, pat in [("HEAD", r'textCentered\("([^"]+)"'),
                      ("LABEL", r'textCentered\("([^"]+)", LABEL_Y'),
                      ("HINT", r'textCentered\("([^"]+)", HINT_Y'),
                      ("NOCODEC", r'textCentered\("([^"]+)"\s*,?\s*VALUE_Y \+ 40')]:
        m = re.search(pat, src)
        if m:
            out[name] = m.group(1)
    # The register line's format specifically: the first snprintf in the file is the
    # percentage, so matching the first one grabs the wrong string.
    m = re.search(r'MENU_LABEL\[MENU_COUNT\] = \{(.*?)\}', src)
    if m:
        out["MENU_LABEL"] = re.findall(r'"([^"]+)"', m.group(1))
    m = re.search(r'snprintf\(buf, sizeof\(buf\), "([^"]*0x32[^"]*)"', src)
    if m:
        out["REG_FMT"] = m.group(1)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--volume", type=int, default=45)
    ap.add_argument("--raw", type=int, default=None, help="codec register, default from --volume")
    ap.add_argument("--no-codec", action="store_true", help="draw the not-answering state")
    ap.add_argument("--page", default="volume",
                    choices=["menu", "volume", "system", "net"], help="which page to draw")
    ap.add_argument("--battery", type=int, default=81)
    ap.add_argument("--low", action="store_true", help="battery below the warning line")
    ap.add_argument("--out", default="build/preview/settings.png")
    args = ap.parse_args()

    K = constants(SRC)
    for key in ("BAR_X0", "BAR_X1", "BAR_Y", "BAR_H", "KNOB_R", "BTN_A_X", "BTN_B_X",
                "BTN_R", "BTN_Y", "VALUE_Y", "VALUE_SCALE", "HINT_Y", "REG_Y", "LABEL_Y",
                "HEAD_Y"):
        if key not in K:
            raise SystemExit(f"preview: {key} not found in {SRC}")
    font = load_font(os.path.join(ROOT, "system", "assets_font.h"))
    raw = args.raw if args.raw is not None else (args.volume * 256 // 100 - 1)

    img = Image.new("RGB", (SCR_W, SCR_H), K["COL_BG"])
    px = img.load()

    def rect(x, y, w, h, c):
        for yy in range(int(y), int(y + h)):
            for xx in range(int(x), int(x + w)):
                if 0 <= xx < SCR_W and 0 <= yy < SCR_H:
                    px[xx, yy] = c

    def disc(cx, cy, r, c):
        for yy in range(int(cy - r) - 1, int(cy + r) + 2):
            for xx in range(int(cx - r) - 1, int(cx + r) + 2):
                if (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r:
                    rect(xx, yy, 1, 1, c)

    def ring(cx, cy, r, c, t=2.0):
        """Solid, the way gfx::strokeCircle draws it -- iterating the bounding box
        rather than stepping round the circle, which left gaps."""
        import math
        for yy in range(int(cy - r - t), int(cy + r + t) + 1):
            for xx in range(int(cx - r - t), int(cx + r + t) + 1):
                d = math.hypot(xx - cx, yy - cy)
                if r - t <= d <= r:
                    rect(xx, yy, 1, 1, c)

    accent, dim, text = K["COL_ACCENT"], K["COL_DIM"], K["COL_TEXT"]
    warn = (0xE0, 0x50, 0x50)

    def back():
        text_centered(px, font, "BACK", K["BACK_Y"], accent, 1)

    if args.page == "menu":
        text_centered(px, font, "SETTINGS", K["HEAD_Y"], accent, 1)
        labels = K.get("MENU_LABEL", ["VOLUME", "SYSTEM", "NET"])
        for i, label in enumerate(labels):
            step = K["MENU_SPAN"] / (len(labels) - 1)
            import math
            a = math.radians(-K["MENU_SPAN"] * 0.5 + step * i)
            sx = 180 + math.sin(a) * K["MENU_R"]
            sy = 180 + math.cos(a) * K["MENU_R"]
            ring(sx, sy, K["MENU_HIT"] * 0.88, tuple(int(v * 0.45) for v in accent))
            draw_text(px, font, label, int(sx - text_width(font, label, 1) * 0.5),
                      int(sy) - 4, accent, 1)
        shown_b = 12 if args.low else args.battery
        text_centered(px, font, "battery %d%%" % shown_b, 104,
                      warn if shown_b < 20 else dim, 1)

    elif args.page == "volume":
        back()
        text_centered(px, font, K["LABEL"], K["LABEL_Y"], dim, 1)
        shown_v = 0 if args.no_codec else args.volume
        text_centered(px, font, "%d%%" % shown_v, K["VALUE_Y"], text, K["VALUE_SCALE"])
        if args.no_codec:
            text_centered(px, font, K.get("NOCODEC", "codec not answering"),
                          K["VALUE_Y"] + 40, dim, 1)
        y = K["BAR_Y"]
        rect(K["BAR_X0"], y, K["BAR_X1"] - K["BAR_X0"], K["BAR_H"], K["COL_TRACK"])
        fill = (K["BAR_X1"] - K["BAR_X0"]) * shown_v // 100
        if fill > 0:
            rect(K["BAR_X0"], y, fill, K["BAR_H"], tuple(int(v * 0.35) for v in accent))
        for i in range(10):
            rect(K["BAR_X0"] + (K["BAR_X1"] - K["BAR_X0"]) * i // 10, y, 1, K["BAR_H"],
                 K["COL_BG"])
        kx = K["BAR_X0"] + (K["BAR_X1"] - K["BAR_X0"]) * shown_v / 100
        disc(kx, y + K["BAR_H"] / 2, K["KNOB_R"], accent)
        disc(kx, y + K["BAR_H"] / 2, K["KNOB_R"] - 4, tuple(int(v * 0.55) for v in accent))
        text_centered(px, font, K["REG_FMT"] % (0 if args.no_codec else raw),
                      K["REG_Y"], dim, 1)
        for i, bx in enumerate((K["BTN_A_X"], K["BTN_B_X"])):
            ring(bx, K["BTN_Y"], K["BTN_R"], accent, t=2.0)
            rect(bx - 10, K["BTN_Y"] - 2, 21, 4, accent)
            if i:
                rect(bx - 2, K["BTN_Y"] - 10, 4, 21, accent)
        text_centered(px, font, K["HINT"], K["HINT_Y"], dim, 1)

    else:
        back()
        # The values are stand-ins: this checks where the rows land relative to the
        # bezel, which is the part a wrong number would not change.
        if args.page == "system":
            rows = [("chip", "ESP32-S3 r0"), ("cores", "2 @ 240 MHz"),
                    ("flash", "16 MB"), ("psram", "8 MB"),
                    ("heap", "148 KB free"), ("sketch", "549 KB"),
                    ("build", "Oct 14 2026"), ("uptime", "1h 23m"),
                    ("battery", "%d%%  4112 mV" % (12 if args.low else args.battery)),
                    ("running", "factory")]
        else:
            rows = [("wifi", "src"), ("key", "set"),
                    ("mac", "44:BD:8D:78:0D:AC"), ("server", "api.tenclass.net"),
                    ("device", "7b55ee14")]
        for i, (label, value) in enumerate(rows):
            y = K["ROW_Y0"] + i * K["ROW_PITCH"]
            draw_text(px, font, label, K["COL_LABEL"], y, dim, 1)
            col = warn if (label == "battery" and (12 if args.low else args.battery) < 20)                 else text
            draw_text(px, font, value, K["COL_VALUE"], y, col, 1)
        if args.page == "net":
            text_centered(px, font, "the assistant's, not ours",
                          K["ROW_Y0"] + 8 * K["ROW_PITCH"], dim, 1)

    apply_bezel(img)
    finish(img, os.path.join(ROOT, args.out), 2)
    print(f"  bar {K['BAR_X0']}..{K['BAR_X1']} at y={K['BAR_Y']}, "
          f"knobs at {K['BTN_A_X']}/{K['BTN_B_X']} y={K['BTN_Y']} r={K['BTN_R']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
