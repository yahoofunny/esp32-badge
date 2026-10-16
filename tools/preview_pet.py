#!/usr/bin/env python3
"""Render the real unified pet C++ implementation (requires a native g++ compiler).

No duplicated Python drawing model: the same source runs on the badge and here.
"""
import argparse
from pathlib import Path
from PIL import Image
from native_checks import run_checks, ROOT

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--screen', action='store_true', help='only the care screen')
    ap.add_argument('--out', type=Path, default=ROOT/'build/preview/pet_state.png')
    args = ap.parse_args()
    run_checks(['pet_state'])
    names = ['pet-unified'] if args.screen else ['pet-unified','pet-listening','pet-speaking',
                                               'pet-wardrobe','pet-outfit']
    out = Image.new('RGB', (360*len(names),360))
    for i,name in enumerate(names):
        out.paste(Image.open(ROOT/'build'/f'{name}.ppm'),(360*i,0))
    args.out.parent.mkdir(parents=True,exist_ok=True)
    out.save(args.out)
    print(args.out)
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
