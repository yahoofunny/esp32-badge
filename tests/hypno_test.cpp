#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../system/game_hypno.h"
#include <Arduino.h>

namespace {
bool finger=false,released=false;int x=180,y=200;
}
namespace input {
bool touchDown(){return finger;}bool tapped(){return released;}
int tapX(){return x;}int tapY(){return y;}
}
struct HypnoTestAccess {
    static float phase(const GameHypno &g){return g.phase;}
    static float halo(const GameHypno &g){return g.halo;}
    static int speed(const GameHypno &g){return g.speed;}
    static bool paused(const GameHypno &g){return g.paused;}
};

static void frame(GameHypno &g,float dt=.02f) {
    testMillis+=uint32_t(dt*1000);g.update(dt);g.render();
}
static void tap(GameHypno &g,int px,int py) {
    x=px;y=py;finger=true;released=false;frame(g);
    finger=false;released=true;frame(g);released=false;
}
static void ppm(const char *path) {
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n360 360\n255\n");
    for(int y=0;y<360;++y)for(int x=0;x<360;++x) {
        uint16_t c=PX(fb[y*360+x]);
        unsigned char rgb[]={uint8_t(((c>>11)&31)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};
        if((x-180)*(x-180)+(y-180)*(y-180)>180*180)rgb[0]=rgb[1]=rgb[2]=0;
        fwrite(rgb,1,3,f);
    }
    fclose(f);
}

int main(int argc,char **argv) {
    std::vector<uint16_t> buffer(360*360+16,0x53a7);fb=buffer.data()+8;
    GameHypno g;g.enter();g.render();assert(g.dirtyY0==0 && g.dirtyY1==359);
    assert(HypnoTestAccess::speed(g)==1 && !HypnoTestAccess::paused(g));
    ppm("build/hypno-centre.ppm");
    // Faster input/render loops do not cause duplicate LCD transfers.
    testMillis+=10;g.render();assert(g.dirtyY1<g.dirtyY0);
    testMillis+=23;g.render();assert(g.dirtyY1>=g.dirtyY0);
    tap(g,180,200);assert(HypnoTestAccess::paused(g));
    const float stopped=HypnoTestAccess::phase(g),halo=HypnoTestAccess::halo(g);
    const std::vector<uint16_t> frozen(fb,fb+360*360);
    for(int i=0;i<100;++i)frame(g);
    assert(HypnoTestAccess::phase(g)==stopped && HypnoTestAccess::halo(g)==halo);
    assert(g.dirtyY1<g.dirtyY0 && memcmp(frozen.data(),fb,360*360*2)==0);
    ppm("build/hypno-paused.ppm");
    // A held control only acts once on release, with immediate visual feedback.
    x=248;y=306;finger=true;frame(g);assert(g.dirtyY1>=g.dirtyY0);
    for(int i=0;i<50;++i)frame(g);
    assert(HypnoTestAccess::speed(g)==1);
    finger=false;released=true;frame(g);released=false;
    assert(HypnoTestAccess::speed(g)==2 && HypnoTestAccess::paused(g));
    for(int i=0;i<5;++i)tap(g,248,306);
    assert(HypnoTestAccess::speed(g)==2);
    for(int i=0;i<6;++i)tap(g,112,306);
    assert(HypnoTestAccess::speed(g)==0);
    tap(g,180,306);assert(!HypnoTestAccess::paused(g));
    // A swipe across the face does not toggle pause or accidentally hit a button.
    x=120;y=190;finger=true;frame(g);x=220;frame(g);
    finger=false;released=true;frame(g);released=false;
    assert(!HypnoTestAccess::paused(g) && HypnoTestAccess::speed(g)==0);
    // Bounded phase over long sessions, extreme dt inputs and uint32 clock wrap.
    for(int i=0;i<20000;++i)g.update(.1f);
    assert(std::isfinite(HypnoTestAccess::phase(g)) && HypnoTestAccess::phase(g)<2*PI_F);
    g.update(NAN);g.update(-1);g.update(INFINITY);
    assert(std::isfinite(HypnoTestAccess::phase(g)));
    testMillis=0xfffffff0;g.render();testMillis=0x30;g.render();
    assert(g.dirtyY1>=g.dirtyY0);
    g.leave();g.enter();assert(!HypnoTestAccess::paused(g) && HypnoTestAccess::speed(g)==1);
    g.render();
    for(int i=0;i<8;++i)assert(buffer[i]==0x53a7 && buffer[buffer.size()-1-i]==0x53a7);
    // Actual framebuffer fixtures, shared with the README and animated preview.
    g.enter();
    const bool animation=argc>1 && std::string(argv[1])=="--animation";
    for(int i=0;i<72;++i) {
        frame(g,2.8f/72);
        if(i==17)ppm("build/hypno-right.ppm");
        if(i==53)ppm("build/hypno-left.ppm");
        if(animation){char path[64];snprintf(path,sizeof path,"build/hypno-frame-%03d.ppm",i);ppm(path);}
    }
    puts("PASS: HYPNO pause/resume, held touch, speed bounds, drag cancel, 30fps pacing, paused LCD silence and timer wrap");
}
