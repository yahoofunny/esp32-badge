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
float acceleration[3]={0,0,1};bool sensor=true;
}
namespace input {
bool touchDown(){return finger;}bool tapped(){return released;}
int tapX(){return x;}int tapY(){return y;}
bool latestRawAccel(float out[3]){memcpy(out,acceleration,sizeof acceleration);return sensor;}
}
struct HypnoTestAccess {
    static float angle(const GameHypno &g){return g.motion.angle;}
    static float velocity(const GameHypno &g){return g.motion.velocity;}
    static float halo(const GameHypno &g){return g.halo;}
    static int sensitivity(const GameHypno &g){return g.sensitivity;}
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
    GameHypno g;g.enter();frame(g);assert(g.dirtyY0==0 && g.dirtyY1==359);
    assert(HypnoTestAccess::sensitivity(g)==1 && !HypnoTestAccess::paused(g));
    ppm("build/hypno-centre.ppm");
    // A resting sensor causes no autonomous movement or redundant transfers.
    testMillis+=10;g.render();assert(g.dirtyY1<g.dirtyY0);
    testMillis+=23;g.render();assert(g.dirtyY1<g.dirtyY0);
    acceleration[0]=.6f;for(int i=0;i<8;++i)frame(g);
    acceleration[0]=0;frame(g);
    assert(HypnoTestAccess::angle(g)>.05f);
    tap(g,180,200);assert(HypnoTestAccess::paused(g));
    const float stopped=HypnoTestAccess::angle(g),halo=HypnoTestAccess::halo(g);
    const float velocity=HypnoTestAccess::velocity(g);
    const std::vector<uint16_t> frozen(fb,fb+360*360);
    for(int i=0;i<100;++i)frame(g);
    assert(HypnoTestAccess::angle(g)==stopped && HypnoTestAccess::halo(g)==halo);
    assert(HypnoTestAccess::velocity(g)==velocity);
    assert(g.dirtyY1<g.dirtyY0 && memcmp(frozen.data(),fb,360*360*2)==0);
    ppm("build/hypno-paused.ppm");
    // A held control only acts once on release, with immediate visual feedback.
    x=248;y=306;finger=true;frame(g);assert(g.dirtyY1>=g.dirtyY0);
    for(int i=0;i<50;++i)frame(g);
    assert(HypnoTestAccess::sensitivity(g)==1);
    finger=false;released=true;frame(g);released=false;
    assert(HypnoTestAccess::sensitivity(g)==2 && HypnoTestAccess::paused(g));
    for(int i=0;i<5;++i)tap(g,248,306);
    assert(HypnoTestAccess::sensitivity(g)==2);
    for(int i=0;i<6;++i)tap(g,112,306);
    assert(HypnoTestAccess::sensitivity(g)==0);
    tap(g,180,306);assert(!HypnoTestAccess::paused(g));
    // A swipe across the face does not toggle pause or accidentally hit a button.
    x=120;y=190;finger=true;frame(g);x=220;frame(g);
    finger=false;released=true;frame(g);released=false;
    assert(!HypnoTestAccess::paused(g) && HypnoTestAccess::sensitivity(g)==0);
    // Without further movement the pendulum settles completely.
    for(int i=0;i<20000;++i)g.update(.1f);
    assert(HypnoTestAccess::angle(g)==0 && HypnoTestAccess::velocity(g)==0);
    g.update(NAN);g.update(-1);g.update(INFINITY);
    assert(std::isfinite(HypnoTestAccess::angle(g)));
    g.enter();frame(g);testMillis=0xfffffff0;
    acceleration[0]=.7f;g.update(.05f);g.render();testMillis=0x30;g.update(.05f);g.render();
    assert(g.dirtyY1>=g.dirtyY0);
    g.leave();g.enter();assert(!HypnoTestAccess::paused(g) && HypnoTestAccess::sensitivity(g)==1);
    g.render();
    for(int i=0;i<8;++i)assert(buffer[i]==0x53a7 && buffer[buffer.size()-1-i]==0x53a7);
    // Actual framebuffer fixtures, shared with the README and animated preview.
    g.enter();acceleration[0]=0;frame(g);
    const bool animation=argc>1 && std::string(argv[1])=="--animation";
    float bestRight=0,bestLeft=0;
    for(int i=0;i<72;++i) {
        // This preview supplies synthetic acceleration; the firmware has no
        // automatic swing. Stop the simulated hand after the first 40 frames.
        acceleration[0]=i<40?.65f*std::sin(i*2*PI_F/30):0;
        frame(g,.04f);
        const float angle=HypnoTestAccess::angle(g);
        if(angle>bestRight){bestRight=angle;ppm("build/hypno-right.ppm");}
        if(angle<bestLeft){bestLeft=angle;ppm("build/hypno-left.ppm");}
        if(animation){char path[64];snprintf(path,sizeof path,"build/hypno-frame-%03d.ppm",i);ppm(path);}
    }
    assert(bestRight>.1f && bestLeft<-.1f);
    puts("PASS: HYPNO motion input, pause/resume, sensitivity bounds, drag cancel, idle LCD silence, settling and timer wrap");
}
