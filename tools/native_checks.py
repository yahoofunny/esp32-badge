#!/usr/bin/env python3
"""Build and execute firmware regression checks on the host, without a serial port."""
import os
import shutil
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
TESTS={
    'power_status':['tests/power_status_test.cpp'],
    'power_recovery':['tests/power_recovery_test.cpp'],
    'gauge_capacity':['tests/gauge_capacity_test.cpp'],
    'photo_blit':['tests/photo_blit_test.cpp'],
    'launcher_drag':['tests/launcher_drag_test.cpp','system/gfx.cpp'],
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
        binary=ROOT/'build'/(name+'_test'+('.exe' if os.name=='nt' else ''))
        subprocess.run([compiler,'-std=c++17','-O2','-static','-Itests/native',
            *TESTS[name],'-o',str(binary)],cwd=ROOT,env=env,check=True)
        subprocess.run([str(binary)],cwd=ROOT,env=env,check=True)

if __name__=='__main__':run_checks()
