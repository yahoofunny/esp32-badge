#!/usr/bin/env python3
"""Render the actual settings POWER methods with a deterministic charging fixture."""
import os
import re
import shutil
import subprocess
from pathlib import Path
from PIL import Image
from native_checks import ROOT
from preview_readme import function_source

def main():
    src=(ROOT/'system/game_settings.cpp').read_text(encoding='utf-8')
    output=ROOT/'build/power-preview'
    output.mkdir(parents=True,exist_ok=True)
    header=(ROOT/'system/game_settings.h').as_posix()
    power=(ROOT/'system/power.h').as_posix()
    constants=[]
    for name in ('COL_BG','COL_ACCENT','COL_TEXT','COL_DIM','COL_TRACK','BACK_Y'):
        constants.append(re.search(r'static const (?:uint16_t|int)\s+'+name+r'\s*=.*?;',src).group())
    code=f'#include "{header}"\n#include "{power}"\n#include <cstdio>\n#include <vector>\n'
    code+='\n'.join(constants)+'\n'
    code+=function_source(src,'void GameSettings::renderBack()')+'\n'
    code+=function_source(src,'void GameSettings::renderPower()')+'\n'
    code+='''
namespace power {Reading cached(){Reading r;r.valid=true;r.percent=84;r.millivolts=4040;
    r.milliamps=180;r.state=ChargeState::Charging;return r;}}
void GameSettings::enter(){} void GameSettings::leave(){} void GameSettings::update(float){}
void GameSettings::drawIcon(float,float,float)const{}
void GameSettings::render(){renderPower();}
int main(){
    std::vector<uint16_t> pixels(SCR_W*SCR_H);fb=pixels.data();GameSettings page;page.render();
    FILE* f=fopen("build/power-preview/power.ppm","wb");if(!f)return 1;
    fprintf(f,"P6\\n360 360\\n255\\n");
    for(int y=0;y<SCR_H;++y)for(int x=0;x<SCR_W;++x){uint16_t c=PX(fb[y*SCR_W+x]);
        unsigned char p[]={static_cast<unsigned char>(((c>>11)&31)*255/31),
            static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};
        if((x-180)*(x-180)+(y-180)*(y-180)>180*180)p[0]=p[1]=p[2]=0;
        fwrite(p,1,3,f);}
    fclose(f);
}
'''
    source=output/'power.cpp';source.write_text(code,encoding='utf-8')
    compiler=shutil.which('g++')
    if not compiler:raise SystemExit('Native g++ is required')
    env=os.environ.copy();env['PATH']=str(Path(compiler).parent)+os.pathsep+env.get('PATH','')
    binary=output/('power.exe' if os.name=='nt' else 'power')
    subprocess.run([compiler,'-std=c++17','-O2','-static','-Itests/native',str(source),
        'system/gfx.cpp','-o',str(binary)],cwd=ROOT,env=env,check=True)
    subprocess.run([str(binary)],cwd=ROOT,env=env,check=True)
    destination=ROOT/'docs/images/power.png'
    Image.open(output/'power.ppm').save(destination,optimize=True)
    print(destination)

if __name__=='__main__':main()
