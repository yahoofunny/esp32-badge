#!/usr/bin/env python3
"""Build and execute firmware regression checks on the host, without a serial port."""
import os
import re
import shutil
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
TESTS={
    'power_status':['tests/power_status_test.cpp'],
    'power_recovery':['tests/power_recovery_test.cpp'],
    'gauge_capacity':['tests/gauge_capacity_test.cpp'],
    'hypno':['tests/hypno_test.cpp','system/game_hypno.cpp','system/gfx.cpp'],
    'hypno_motion':['tests/hypno_motion_test.cpp'],
    'music_output':['tests/music_output_test.cpp'],
    'music_task':['tests/music_task_test.cpp','-pthread'],
    'flac_clock':['tests/flac_clock_test.cpp'],
    'transfer_rules':['tests/transfer_rules_test.cpp'],
    'transfer_http':['tests/transfer_http_test.cpp'],
    'photo_blit':['tests/photo_blit_test.cpp'],
    'photo_gif':['tests/photo_gif_test.cpp'],
    'photo_gif_native':['tests/photo_gif_test.cpp','-DCLAWD_NATIVE_RGB565=1'],
    'photo_gesture':['tests/photo_gesture_test.cpp'],
    'launcher_drag':['tests/launcher_drag_test.cpp','system/gfx.cpp'],
    'minigames':['tests/minigames_test.cpp','system/game_snake.cpp','system/game_bricks.cpp','system/gfx.cpp'],
    'frame_timing':['tests/frame_timing_test.cpp'],
    'pet_state':['tests/pet_state_test.cpp','system/game_pet.cpp','system/gfx.cpp','system/outfits.cpp'],
    'outfits':['tests/outfits_test.cpp','system/outfits.cpp','system/gfx.cpp'],
    'outfits_native':['tests/outfits_test.cpp','system/outfits.cpp','system/gfx.cpp','-DCLAWD_NATIVE_RGB565=1'],
    'gfx_circle':['tests/gfx_circle_test.cpp','system/gfx.cpp'],
    'pet_ui_pacing':['tests/pet_ui_pacing_test.cpp'],
}
def run_checks(names=None):
    compiler=shutil.which('g++')
    if not compiler:raise SystemExit('Native g++ is required for firmware checks and previews')
    env=os.environ.copy()
    env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
    (ROOT/'build').mkdir(exist_ok=True)
    for name in names or TESTS:
        if name in ('music_output','music_task','transfer_http','flac_clock','frame_timing'):
            def function(source,signature):
                start=source.index(signature);opening=source.index('{',start)
                depth=0
                # JSON strings and comments can contain unmatched braces.
                tokens=r'''//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}]'''
                for token in re.finditer(tokens,source[opening:]):
                    if token.group()=='{':depth+=1
                    elif token.group()=='}':
                        depth-=1
                        if depth==0:return source[start:opening+token.end()]
                raise ValueError('Unclosed C++ function: '+signature)
            app=(ROOT/'system/game_music.cpp').read_text(encoding='utf-8')
            decoder=(ROOT/'lib/badge_audio_i2s/src/BadgeAudio.cpp').read_text(encoding='utf-8')
            if name=='music_output':
                generated=function(app,'void audio_process_i2s(')+'\n'+function(decoder,'bool Audio::playSample(')
            elif name=='music_task':
                generated=';\n'.join(function(app,signature) for signature in
                    ('struct MusicPump {','struct MusicGuard {','void GameMusic::pumpAudio(',
                     'void GameMusic::leave('))
            elif name=='flac_clock':
                generated=function(decoder,'void Audio::compute_audioCurrentTime(')
            elif name=='frame_timing':
                generated=function((ROOT/'system/system.ino').read_text(encoding='utf-8'),'static float frameSeconds(')
            else:
                transfer=(ROOT/'lib/badge_transfer/src/BadgeTransfer.cpp').read_text(encoding='utf-8')
                generated='\n'.join(function(transfer,signature) for signature in
                    ('static void message(','static esp_err_t reply(',
                     'static String jsonString(','static esp_err_t upload('))
            (ROOT/'build'/(name+'_functions.h')).write_text(generated,encoding='utf-8')
        binary=ROOT/'build'/(name+'_test'+('.exe' if os.name=='nt' else ''))
        subprocess.run([compiler,'-std=c++17','-O2','-static','-Itests/native',
            *TESTS[name],'-o',str(binary)],cwd=ROOT,env=env,check=True)
        subprocess.run([str(binary)],cwd=ROOT,env=env,check=True)

if __name__=='__main__':run_checks()
