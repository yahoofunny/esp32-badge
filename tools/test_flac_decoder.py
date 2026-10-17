"""Decode real FLAC frames with production C++, compare every sample to FFmpeg."""
import math,os,random,shutil,struct,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/flac-checks'
OUT.mkdir(parents=True,exist_ok=True)
compiler=shutil.which('g++');ffmpeg=shutil.which('ffmpeg')
assert compiler and ffmpeg,'g++ and FFmpeg are required'
env=os.environ.copy();env['PATH']=str(Path(compiler).parent)+os.pathsep+env['PATH']
exe=OUT/'decode.exe'
subprocess.run([compiler,'-std=c++17','-O2','-static','-Itests/native/flac',
    'tests/flac_decoder_test.cpp','lib/badge_audio_i2s/src/flac_decoder/flac_decoder.cpp',
    '-o',str(exe)],cwd=ROOT,env=env,check=True)
rng=random.Random(919)
for bits,ch,mode,level,block in [(24,2,'tone',8,4096),(24,1,'tone',12,8192),
        (24,2,'noise',0,8192),(24,2,'constant',8,4096),(16,2,'tone',8,4096),
        (16,1,'noise',0,4096)]:
    name=f'{bits}-{ch}-{mode}';raw=OUT/(name+'.raw');flac=OUT/(name+'.flac')
    n=19007;buf=bytearray();peak=(1<<(bits-1))-1
    for i in range(n):
        for c in range(ch):
            v=(int(peak*(.61*math.sin(i*.072+c*.1)+.24*math.sin(i*.0037))) if mode=='tone'
               else rng.randint(-peak,peak) if mode=='noise' else peak//3)
            buf+=int(v).to_bytes(bits//8,'little',signed=True)
    raw.write_bytes(buf)
    subprocess.run([ffmpeg,'-v','error','-y','-f',f's{bits}le','-ar','48000','-ac',str(ch),
        '-i',str(raw),'-c:a','flac','-compression_level',str(level),'-frame_size',str(block),str(flac)],check=True)
    reference=OUT/(name+'.ref');actual=OUT/(name+'.pcm')
    subprocess.run([ffmpeg,'-v','error','-y','-i',str(flac),'-f','s16le',str(reference)],check=True)
    subprocess.run([str(exe),str(flac),str(actual)],env=env,check=True)
    expected=reference.read_bytes();got=actual.read_bytes()
    assert got==expected, f'{name}: PCM mismatch at byte '+str(next((i for i,(a,b) in enumerate(zip(got,expected)) if a!=b),-1))
print('PASS: all production FLAC PCM samples match FFmpeg (24/16-bit, stereo/mono, LPC, constant and verbatim)')
