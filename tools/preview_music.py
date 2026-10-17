#!/usr/bin/env python3
"""Render the player's actual C++ drawing with a deterministic music fixture."""
import os
import re
import shutil
import subprocess
from pathlib import Path
from PIL import Image
from native_checks import ROOT
from preview_readme import function_source

source=(ROOT/'system/game_music.cpp').read_text(encoding='utf-8')
folder=ROOT/'build/music-preview';folder.mkdir(parents=True,exist_ok=True)
parts=['#include <Arduino.h>','#include <cmath>','#include <vector>',
       f'#include "{(ROOT/"system/game_music.h").as_posix()}"']
parts+=re.findall(r'static const uint16_t COL_\w+\s*=.*?;',source)
parts += [function_source(source,'void GameMusic::drawIcon('),
          function_source(source,'void GameMusic::render('),
          'void GameMusic::enter(){} void GameMusic::leave(){} void GameMusic::update(float){}',r'''
struct MusicTestAccess {
 static void fixture(GameMusic &g){g.tracks=3;g.index=0;g.volume=70;g.elapsed=43;g.duration=180;
  g.rotation=.7f;snprintf(g.title,sizeof g.title,"BADGE LOOP");}
};
int main(){
 std::vector<uint16_t> pixels(360*360);fb=pixels.data();GameMusic g;MusicTestAccess::fixture(g);g.render();
 FILE *f=fopen("build/music-preview/music.ppm","wb");if(!f)return 1;
 fprintf(f,"P6\n360 360\n255\n");
 for(int y=0;y<360;++y)for(int x=0;x<360;++x){
  uint16_t c=PX(fb[y*360+x]);unsigned char rgb[]={uint8_t(((c>>11)&31)*255/31),
   uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};
  if((x-180)*(x-180)+(y-180)*(y-180)>180*180)rgb[0]=rgb[1]=rgb[2]=0;
  fwrite(rgb,1,3,f);
 }
 fclose(f);
}
''']
cpp=folder/'music.cpp';cpp.write_text('\n'.join(parts),encoding='utf-8')
binary=folder/('music.exe' if os.name=='nt' else 'music')
compiler=shutil.which('g++');env=os.environ.copy()
env['PATH']=str(Path(compiler).parent)+os.pathsep+env['PATH']
subprocess.run([compiler,'-std=c++17','-O2','-static','-Itests/native',str(cpp),'system/gfx.cpp','-o',str(binary)],cwd=ROOT,env=env,check=True)
subprocess.run([str(binary)],cwd=ROOT,env=env,check=True)
output=ROOT/'docs/images/music.png'
Image.open(folder/'music.ppm').save(output,optimize=True)
print(output)
