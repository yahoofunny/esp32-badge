"""Check launcher pixels and host rendering cost against an earlier Git revision.

Usage: python tools/check_launcher_render.py [--baseline HEAD]
Uses a separate build directory and never connects to or flashes the badge.
Host timings do not measure ESP32 performance, PSRAM or LCD transfer time.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', default='HEAD', help='Git revision before optimization')
    args = parser.parse_args()
    compiler = shutil.which('g++')
    if not compiler:
        raise SystemExit('Native g++ is required')
    output = ROOT / 'build/launcher-perf-check'
    output.mkdir(parents=True, exist_ok=True)
    baseline = output / 'launcher_baseline.cpp'
    source = subprocess.check_output(
        ['git', 'show', f'{args.baseline}:system/launcher.cpp'], cwd=ROOT)
    baseline.write_bytes(source)
    env = os.environ.copy()
    env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
    for native in (False, True):
        binary = output / ('native' if native else 'panel')
        if os.name == 'nt':
            binary = binary.with_suffix('.exe')
        command = [compiler, '-std=c++17', '-O2', '-static', '-Itests/native', '-Isystem',
                   f'-DLAUNCHER_BASELINE="{baseline.as_posix()}"']
        if native:
            command.append('-DCLAWD_NATIVE_RGB565=1')
        command += ['tests/launcher_render_test.cpp', 'system/gfx.cpp', '-o', str(binary)]
        subprocess.run(command, cwd=ROOT, env=env, check=True)
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)

if __name__ == '__main__':
    main()
