#!/usr/bin/env python3
"""Render the real HYPNO C++ framebuffer, optionally as an animated GIF."""
import argparse
import os
import subprocess
from PIL import Image
from native_checks import ROOT,run_checks

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse-frames',action='store_true')
    parser.add_argument('--animation',action='store_true')
    args=parser.parse_args()
    if not args.reuse_frames:run_checks(['hypno'])
    destination=ROOT/'docs/images'
    destination.mkdir(parents=True,exist_ok=True)
    Image.open(ROOT/'build/hypno-right.ppm').save(destination/'hypno.png',optimize=True)
    print(destination/'hypno.png')
    if args.animation:
        binary=ROOT/'build'/('hypno_test.exe' if os.name=='nt' else 'hypno_test')
        subprocess.run([str(binary),'--animation'],cwd=ROOT,check=True)
        frames=[]
        for i in range(72):
            with Image.open(ROOT/f'build/hypno-frame-{i:03d}.ppm') as frame:
                frames.append(frame.convert('RGB').copy())
        output=ROOT/'build/hypno-preview.gif'
        frames[0].save(output,save_all=True,append_images=frames[1:],duration=40,loop=0,optimize=False)
        print(output)

if __name__=='__main__':main()
