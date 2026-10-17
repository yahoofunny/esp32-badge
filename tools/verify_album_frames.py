#!/usr/bin/env python3
"""Compare the real firmware GIF compositor/decoder against Pillow, every pixel/frame."""
import argparse
from pathlib import Path
import subprocess
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gif-library',type=Path,
        default=Path.home()/'Documents/Arduino/libraries/AnimatedGIF/src')
    args=parser.parse_args()
    binary=ROOT/'build/photo_gif_decode.exe'
    binary.parent.mkdir(exist_ok=True)
    subprocess.run(['g++','-std=c++17','-O2','-static','-D__LINUX__',
        '-DreadFLASH=readMem','-I',str(args.gif_library),
        str(ROOT/'tests/photo_gif_decode.cpp'),str(args.gif_library/'AnimatedGIF.cpp'),
        '-o',str(binary)],check=True)
    output=ROOT/'build/gif-test.raw'
    total=0
    for path in sorted((ROOT/'assets/clawd-gifs').glob('*.gif')):
        subprocess.run([str(binary),str(path),str(output)],check=True,capture_output=True)
        with Image.open(path) as image:
            frames=np.fromfile(output,dtype='<u2').reshape(-1,image.height,image.width)
            assert len(frames)==image.n_frames,(path,len(frames),image.n_frames)
            for n,actual in enumerate(frames):
                image.seek(n)
                rgba=np.asarray(image.convert('RGBA'))
                rgb=rgba[:,:,:3].astype(np.uint16)
                rgb[rgba[:,:,3]==0]=0
                expected=((rgb[:,:,0]>>3)<<11)|((rgb[:,:,1]>>2)<<5)|(rgb[:,:,2]>>3)
                assert np.array_equal(actual,expected),(path.name,n,int(np.count_nonzero(actual!=expected)))
            total+=image.n_frames
        print(f'PASS {path.name}: {len(frames)} complete RGB565 frames',flush=True)
    output.unlink()
    print(f'PASS: all {total} animation frames match independent Pillow decoding pixel for pixel')

if __name__=='__main__':main()
