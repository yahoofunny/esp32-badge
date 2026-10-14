#!/usr/bin/env python3
"""Check that the pet preview still mirrors the firmware.

tools/preview_pet.py re-implements the pet screen's layout and drawing in Python so it
can be checked on the host instead of on the device. That only works while its
constants agree with system/game_pet.cpp -- and they have drifted twice already: the
menu label BATHE outlived the firmware's SCRUB, and HEALTH_Y was two pixels out. A
preview that lies is worse than no preview, because it is trusted.

    tools/check_preview_sync.py     # exit 1 if anything has drifted
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CPP = ROOT / "system" / "game_pet.cpp"
PREVIEW = ROOT / "tools" / "preview_pet.py"

EYE_ORDER = ["EYE_ROUND", "EYE_HAPPY", "EYE_SHUT", "EYE_CROSS", "EYE_ANGRY"]
EMOTE_ORDER = ["EMOTE_NONE", "EMOTE_HEART", "EMOTE_ANGER", "EMOTE_SICK", "EMOTE_ZZZ"]
MOODS = [("IDLE", "idle"), ("HAPPY", "happy"), ("EAT", "eat"), ("SLEEP", "sleep"),
         ("BATH", "bath"), ("SICK", "sick"), ("ANNOYED", "annoyed")]


def main() -> int:
    cpp = CPP.read_text(encoding="utf-8")
    preview = PREVIEW.read_text(encoding="utf-8")
    bad = []

    def check(name, from_cpp, from_preview):
        if from_cpp != from_preview:
            bad.append(f"  {name}: firmware {from_cpp!r}  !=  preview {from_preview!r}")

    def scalar(text, name):
        # Not anchored to the line start: several of these share a declaration
        # (`MENU_Y0, MENU_PITCH = 278, 18`), and the preview indents its own.
        m = re.search(rf"(?<![\w]){name}\s*=\s*(\d+)", text)
        return int(m.group(1)) if m else None

    def pair(text, a, b):
        # `A = 1, B = 2`, or `A, B = 1, 2`, or two declarations on separate lines --
        # the firmware and the preview do not have to be written the same way, and
        # insisting they are is how this check starts failing for the wrong reason.
        for pattern in (rf"{a}\s*=\s*(\d+),\s*{b}\s*=\s*(\d+)",
                        rf"{a},\s*{b}\s*=\s*(\d+),\s*(\d+)"):
            found = re.search(pattern, text)
            if found:
                return int(found.group(1)), int(found.group(2))
        ma = re.search(rf"(?<![\w]){a}\s*=\s*(\d+)", text)
        mb = re.search(rf"(?<![\w]){b}\s*=\s*(\d+)", text)
        return (int(ma.group(1)), int(mb.group(1))) if ma and mb else None

    # --- layout: every coordinate the two draw at ---------------------------------
    for key in ["HEAD_Y", "RULE1_Y", "HEALTH_Y", "BUBBLE_CY", "BUBBLE_RX",
                "BUBBLE_RXMIN", "BUBBLE_TAIL", "BUBBLE_RYMIN", "SPEECH_PAD",
                "SPEECH_LINE", "SPEECH_MAXLINES", "SPEECH_COLS", "BAR_CHARS"]:
        check(key, scalar(cpp, key), scalar(preview, key))

    for a, b in [("METER_COL_A", "METER_COL_B"), ("METER_ROW0", "METER_ROW1"),
                 ("RULE_X0", "RULE_X1"), ("PET_CX", "PET_CY")]:
        check(f"{a}/{b}", pair(cpp, a, b), pair(preview, a, b))

    # --- colours ------------------------------------------------------------------
    for key in ["COL_BG", "COL_TEXT", "COL_DIM", "COL_RULE", "COL_ACCENT", "COL_WARN",
                "COL_PANEL"]:
        m = re.search(rf"{key}\s*=\s*PX\(RGB565\((0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+), "
                      rf"(0x[0-9A-Fa-f]+)\)\)", cpp)
        n = re.search(rf"^{key}\s*=\s*\((0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+), "
                      rf"(0x[0-9A-Fa-f]+)\)", preview, re.M)
        check(key, m and tuple(int(x, 16) for x in m.groups()),
              n and tuple(int(x, 16) for x in n.groups()))

    # --- the mood table -----------------------------------------------------------
    table = re.search(r"MOODS\[ANIM_COUNT\] = \{(.*?)\n\};", cpp, re.S)
    rows = {}
    for line in table.group(1).splitlines():
        m = re.match(r"\s*/\* (\w+)\s*\*/ \{ (\w+),\s*(\w+),\s*(true|false),\s*(\d+) \}",
                     line)
        if m:
            rows[m.group(1)] = (EYE_ORDER.index(m.group(2)),
                                EMOTE_ORDER.index(m.group(3)),
                                m.group(4) == "true", int(m.group(5)))
    # The preview spells the mood tuples with the C++ names, so compare token lists
    # rather than values -- that way a renamed enum shows up here too.
    for cpp_name, preview_name in MOODS:
        m = re.search(rf'^\s*"{preview_name}": \((.*?)\),\s*$', preview, re.M)
        if not m or cpp_name not in rows:
            bad.append(f"  MOODS.{preview_name}: missing from "
                       f"{'the firmware' if cpp_name not in rows else 'the preview'}")
            continue
        seen = [t.strip() for t in m.group(1).split(",")]
        want = [EYE_ORDER[rows[cpp_name][0]], EMOTE_ORDER[rows[cpp_name][1]],
                "True" if rows[cpp_name][2] else "False", str(rows[cpp_name][3])]
        check(f"MOODS.{preview_name}", want, seen)

    # --- what the menu renders ----------------------------------------------------
    labels = re.findall(r'"(\w+)"', re.search(r"ACT_LABEL\[ACT_COUNT\] = \{(.*?)\}",
                                              cpp).group(1))
    check("ACT_LABEL", labels,
          re.search(r"^ACT_LABELS = \[(.*?)\]", preview, re.M).group(1)
          .replace('"', "").split(", "))

    # The icon's proportions are not checked here: preview_pet.py parses them out of
    # GamePet::drawIcon, so there is no second copy to drift.

    # The arc menu's geometry: a float, so not in the scalar list above.
    for key in ["MENU_R", "MENU_SPAN", "MENU_HIT"]:
        m = re.search(rf"{key}\s*=\s*([0-9.]+)f", cpp)
        n = re.search(rf"^{key}\s*=\s*([0-9.]+)$", preview, re.M)
        check(key, m and float(m.group(1)), n and float(n.group(1)))

    # --- and the stage names ------------------------------------------------------
    check("STAGE_NAMES",
          re.findall(r'"(\w+)"', re.search(r"STAGE_NAMES\[4\] = \{(.*?)\}", cpp).group(1)),
          re.findall(r'"(\w+)"', re.search(r"^STAGE_NAMES = \[(.*?)\]", preview, re.M).group(1)))

    if bad:
        print("preview has drifted from the firmware:")
        print("\n".join(bad))
        return 1
    print(f"preview matches the firmware on {13 + 5 + 3 + 7 + len(MOODS) + 2} mirrored values")
    return 0


if __name__ == "__main__":
    sys.exit(main())
