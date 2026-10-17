#!/usr/bin/env python3
"""Render the real transfer screen with fictional hotspot credentials."""
import os
import re
import shutil
import subprocess
from pathlib import Path
from PIL import Image
from native_checks import ROOT
from preview_readme import function_source

source=(ROOT/'system/game_settings.cpp').read_text(encoding='utf-8')
folder=ROOT/'build/transfer-preview';folder.mkdir(parents=True,exist_ok=True)
parts=['#include <Arduino.h>','#include <vector>','#include <BadgeTransfer.h>',
       '#define private public',f'#include "{(ROOT/"system/game_settings.h").as_posix()}"','#undef private']
parts += [line for line in source.splitlines() if line.startswith('static const ')]
parts += [r'''
namespace badge_transfer {void status(Status &s){s=Status{};s.running=true;s.clients=1;s.completed=4;
 snprintf(s.ssid,sizeof s.ssid,"BADGE-DEMO");snprintf(s.password,sizeof s.password,"DEMO1234");}}
void GameSettings::enter(){} void GameSettings::leave(){} void GameSettings::update(float){}
void GameSettings::drawIcon(float,float,float) const{}
void GameSettings::render(){renderTransfer();}
''',function_source(source,'void GameSettings::renderBack('),function_source(source,'void GameSettings::renderTransfer('),r'''
int main(){std::vector<uint16_t> pixels(360*360);fb=pixels.data();GameSettings g;g.render();
 FILE *f=fopen("build/transfer-preview/screen.ppm","wb");if(!f)return 1;fprintf(f,"P6\n360 360\n255\n");
 for(int y=0;y<360;++y)for(int x=0;x<360;++x){uint16_t c=PX(fb[y*360+x]);
  unsigned char rgb[]={uint8_t(((c>>11)&31)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};
  if((x-180)*(x-180)+(y-180)*(y-180)>180*180)rgb[0]=rgb[1]=rgb[2]=0;fwrite(rgb,1,3,f);}
 fclose(f);}
''']
cpp=folder/'screen.cpp';cpp.write_text('\n'.join(parts),encoding='utf-8')
compiler=shutil.which('g++');env=os.environ.copy();env['PATH']=str(Path(compiler).parent)+os.pathsep+env['PATH']
binary=folder/('screen.exe' if os.name=='nt' else 'screen')
subprocess.run([compiler,'-std=c++17','-O2','-static','-Itests/native','-Ilib/badge_transfer/src',
 str(cpp),'system/gfx.cpp','-o',str(binary)],cwd=ROOT,env=env,check=True)
subprocess.run([str(binary)],cwd=ROOT,env=env,check=True)
output=ROOT/'docs/images/wifi-transfer.png';Image.open(folder/'screen.ppm').save(output)
print(output)
