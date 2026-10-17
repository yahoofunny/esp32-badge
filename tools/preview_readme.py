#!/usr/bin/env python3
"""Rebuild README images from the real C++ framebuffer renderers.

Requires g++ and Pillow. No board, serial connection, screenshots, or private
photos are used. The pet fixtures come from tests/pet_state_test.cpp. The
launcher runs system/launcher.cpp with the current system.ino registry and the
original drawIcon methods extracted from each game source. Only unrelated app
lifecycle methods and hardware input/sound are stubbed for the launcher.

These are software renders, not photographs. In particular pet-voice.png is the
shared speaking framebuffer, without the ESP-IDF/LVGL subtitle overlay.
"""
import argparse
import os
import re
import shutil
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw

from native_checks import ROOT, run_checks


def function_source(source, signature):
    """Extract a complete source function, keeping its drawing code unchanged."""
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


def render_launcher(registry_name='GAMES', selected_class='GamePet', output_stem='launcher'):
    compiler = shutil.which('g++')
    if not compiler:
        raise SystemExit('Native g++ is required')
    generated = ROOT / 'build/readme-render'
    generated.mkdir(parents=True, exist_ok=True)
    sketch = (ROOT / 'system/system.ino').read_text(encoding='utf-8')
    registry = re.search(r'\b' + registry_name + r'\[\]\s*=\s*\{([^}]+)\}', sketch).group(1)
    instances = re.findall(r'&(\w+)', registry)
    classes = [re.search(r'static\s+(\w+)\s+' + name + r'\s*;', sketch).group(1)
               for name in instances]
    sources = []
    headers = []
    for cls in classes:
        if cls in ('GameFolder', 'GameBack'):
            headers.append(f'#include "{(ROOT / "system/launcher.h").as_posix()}"')
            continue  # Navigation tile methods are inline in launcher.h.
        stem = re.sub(r'(?<!^)(?=[A-Z])', '_', cls).lower()
        header = ROOT / 'system' / (stem + '.h')
        source = header.with_suffix('.cpp').read_text(encoding='utf-8')
        draw = function_source(source, f'void {cls}::drawIcon(')
        # Each generated translation unit retains the original header accessors,
        # drawing method, and any constants/helpers that method actually uses.
        chunks = [f'#include "{header.as_posix()}"', '#include <cmath>',
                  f'#include "{(ROOT / "system/assets_icons.h").as_posix()}"',
                  f'#include "{(ROOT / "system/assets_mon.h").as_posix()}"']
        for constant in sorted(set(re.findall(r'\bCOL_\w+\b', draw))):
            match = re.search(r'static const uint16_t\s+' + constant + r'\s*=.*?;', source)
            if not match:
                raise RuntimeError(f'Cannot extract {constant} from {stem}')
            chunks.append(match.group(0))
        if 'formOf(' in draw:
            chunks.append('using namespace mon;')
            chunks.append(function_source(source, 'static const mon_form_t &formOf('))
        chunks.extend([draw, f'void {cls}::enter() {{}}',
                       f'void {cls}::update(float) {{}}', f'void {cls}::render() {{}}'])
        if re.search(r'\bvoid\s+leave\s*\(', header.read_text(encoding='utf-8')):
            chunks.append(f'void {cls}::leave() {{}}')
        path = generated / (stem + '.cpp')
        path.write_text('\n'.join(chunks) + '\n', encoding='utf-8')
        sources.append(str(path))
        headers.append(f'#include "{header.as_posix()}"')

    harness = '\n'.join(headers) + r'''
#include <cstdio>
#include <vector>
#include "LAUNCHER_SOURCE"
namespace input {
bool touchDown(){return false;} bool tapped(){return false;}
int tapX(){return 0;} int tapY(){return 0;} float tiltX(){return 0;}
}
namespace sound {void play2(int,int,int,int){} void launch(){}}
int main() {
    std::vector<uint16_t> pixels(SCR_W*SCR_H);fb=pixels.data();
    REGISTRY_INSTANCES
    Game* games[]={REGISTRY_POINTERS};
    launcher::begin(games,sizeof(games)/sizeof(games[0]));
    // Deterministic fixture: an actual registered entry between gestures.
    launcher::sel=SELECTED_INDEX;launcher::selPos=SELECTED_INDEX;launcher::update(.25f);
    launcher::render();
    FILE* f=fopen("build/readme-render/OUTPUT_STEM.ppm","wb");if(!f)return 1;
    fprintf(f,"P6\n360 360\n255\n");
    for(int y=0;y<SCR_H;++y)for(int x=0;x<SCR_W;++x){
        uint16_t c=PX(fb[y*SCR_W+x]);
        unsigned char px[]={static_cast<unsigned char>(((c>>11)&31)*255/31),
            static_cast<unsigned char>(((c>>5)&63)*255/63),
            static_cast<unsigned char>((c&31)*255/31)};
        if((x-180)*(x-180)+(y-180)*(y-180)>180*180)px[0]=px[1]=px[2]=0;
        fwrite(px,1,3,f);
    }
    fclose(f);
}
'''
    harness = harness.replace('LAUNCHER_SOURCE', (ROOT / 'system/launcher.cpp').as_posix())
    harness = harness.replace('REGISTRY_INSTANCES', '\n'.join(
        f'{cls} {instance};' for cls, instance in zip(classes, instances)))
    harness = harness.replace('REGISTRY_POINTERS', ','.join('&' + x for x in instances))
    harness = harness.replace('SELECTED_INDEX', str(classes.index(selected_class)))
    harness = harness.replace('OUTPUT_STEM', output_stem)
    harness_path = generated / (output_stem + '.cpp')
    harness_path.write_text(harness, encoding='utf-8')
    binary = generated / (output_stem + ('.exe' if os.name == 'nt' else ''))
    env = os.environ.copy()
    env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
    subprocess.run([compiler, '-std=c++17', '-O2', '-static', '-Itests/native',
                    str(harness_path), *sources, 'system/gfx.cpp', '-o', str(binary)],
                   cwd=ROOT, env=env, check=True)
    subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
    return generated / (output_stem + '.ppm')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reuse-pet-frames', action='store_true',
                        help='use frames from a just-completed pet_state check')
    args = parser.parse_args()
    if not args.reuse_pet_frames:
        run_checks(['pet_state'])
    frames = {
        'launcher': render_launcher(),
        'pet-care': ROOT / 'build/pet-care-child-flower.ppm',
        'pet-wardrobe': ROOT / 'build/pet-wardrobe-child-headphones.ppm',
        'pet-voice': ROOT / 'build/pet-speaking.ppm',
    }
    destination = ROOT / 'docs/images'
    destination.mkdir(parents=True, exist_ok=True)
    # Labels sit outside the framebuffer only in the optional contact sheet.
    sheet = Image.new('RGB', (720, 768), '#08080d')
    labels = ['Launcher', 'Pet care / flower', 'Wardrobe / headphones',
              'Speaking framebuffer (no subtitle overlay)']
    for index, (name, frame) in enumerate(frames.items()):
        rendered = Image.open(frame).convert('RGB')
        if rendered.size != (360, 360):
            raise RuntimeError(f'Unexpected framebuffer size: {frame}')
        rendered.save(destination / (name + '.png'), optimize=True)
        x, y = (index % 2) * 360, (index // 2) * 384
        sheet.paste(rendered, (x, y))
        ImageDraw.Draw(sheet).text((x + 12, y + 363), labels[index], fill='#ccccdd')
        print(destination / (name + '.png'))
    sheet.save(ROOT / 'build/readme-preview.png', optimize=True)
    print(ROOT / 'build/readme-preview.png')


if __name__ == '__main__':
    main()
