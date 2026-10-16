#!/usr/bin/env python3
"""Install the integrated pet into ota_0, preserving the launcher and shared assets.

Default is a dry run. --flash reads/backs up the current configuration and voice
slot before writing the verified custom app. No bootloader or partition changes.
"""
import argparse
import hashlib
import subprocess
import sys
from datetime import datetime
from pathlib import Path
from install_stack import find_esptool, describe

ROOT=Path(__file__).resolve().parents[1]
APP=ROOT/'build/clawd-voice/clawd-pet.bin'
PART=ROOT/'build/clawd-voice/partition_table/partition-table.bin'
STOCK=ROOT/'backup/1.85B-stock-16MB.bin'

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--flash',action='store_true')
    ap.add_argument('--port',default='COM3')
    args=ap.parse_args()
    app=APP.read_bytes();table=PART.read_bytes()
    if not app or app[0]!=0xe9 or len(app)>0x400000:
        raise SystemExit('Invalid image or image exceeds the 4MB voice slot')
    if app[0x50:0x70].split(b'\0')[0]!=b'clawd-pet':
        raise SystemExit('Expected a clawd-pet project image')
    with STOCK.open('rb') as f:f.seek(0x8000);expected=f.read(len(table))
    if table!=expected:raise SystemExit('Custom build partition table differs from the backed-up stock layout')
    print(describe(app,'voice-pet'))
    print(f'ota_0 @ 0x6c0000: {len(app)} bytes, sha256={hashlib.sha256(app).hexdigest()}')
    if not args.flash:
        print('Dry run. Use --flash to back up and install this image.');return 0
    stamp=datetime.now().strftime('%Y%m%d-%H%M%S')
    cfg=ROOT/f'backup/pre-voice-{stamp}-config.bin'
    previous=ROOT/f'backup/pre-voice-{stamp}-ota0.bin'
    esp=find_esptool()
    base=[esp,'--chip','esp32s3','--port',args.port,'--baud','921600',
          '--after','watchdog_reset','--connect-attempts','1']
    def run(extra,label):
        result=subprocess.run(base+extra,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        (ROOT/'build'/f'voice-{label}.log').write_text(result.stdout,encoding='utf-8')
        if result.returncode:
            print('\n'.join(result.stdout.splitlines()[-8:]));raise SystemExit(result.returncode)
        print(label+': OK')
    run(['read_flash','0x8000','0x7000',str(cfg)],'config-backup')
    if cfg.read_bytes()[:len(table)]!=table:raise SystemExit('Connected board partition table mismatch; no firmware was written')
    run(['read_flash','0x6c0000','0x400000',str(previous)],'previous-voice-backup')
    run(['write_flash','--flash_mode','keep','--flash_size','keep','--flash_freq','keep',
         '0x6c0000',str(APP)],'install')
    print('Installed. Open CLAWD PET from the launcher; hold BOOT in the pet to return.')
    return 0

if __name__=='__main__':sys.exit(main())
