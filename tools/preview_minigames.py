#!/usr/bin/env python3
"""Run the real game logic and render the actual round-screen C++ framebuffers."""
from PIL import Image,ImageDraw
from native_checks import ROOT,run_checks
from preview_readme import render_launcher

if __name__=='__main__':
    run_checks(['minigames'])
    destination=ROOT/'docs/images';destination.mkdir(exist_ok=True)
    for name in ['snake','bricks']:
        Image.open(ROOT/f'build/{name}-playing.ppm').save(destination/f'{name}.png',optimize=True)
    Image.open(render_launcher()).save(destination/'launcher.png',optimize=True)
    sheet=Image.new('RGB',(720,768),'#08080d');draw=ImageDraw.Draw(sheet)
    for i,name in enumerate(['snake-ready','bricks-ready','snake-playing','bricks-playing']):
        x=(i%2)*360;y=(i//2)*384
        sheet.paste(Image.open(ROOT/f'build/{name}.ppm'),(x,y));draw.text((x+12,y+363),name,fill='#ccccdd')
    sheet.save(ROOT/'build/minigames-preview.png');print(ROOT/'build/minigames-preview.png')
