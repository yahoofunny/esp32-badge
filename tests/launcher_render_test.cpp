// Compare the optimized renderer against a supplied pre-change source file.
// Run through tools/check_launcher_render.py; no LCD or serial port is used.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
#include "../system/launcher.h"
#define launcher original_launcher
#include LAUNCHER_BASELINE
#undef launcher
#include "../system/launcher.cpp"

namespace input {
bool touchDown(){return false;} bool tapped(){return false;}
int tapX(){return 0;} int tapY(){return 0;} float tiltX(){return 0;}
}
namespace sound {void play2(int,int,int,int){} void launch(){}}
namespace {
struct Icon:Game {
    int id=0;
    const char* name()const override{return "LAUNCHER";}
    uint16_t accent()const override{return PX(RGB565(id*13+40,200-id*7,80+id*9));}
    void update(float)override{} void render()override{}
    void drawIcon(float x,float y,float size)const override {
        gfx::fillRect(int(x-size/3),int(y-size/4),int(size/2),int(size/2),accent());
        gfx::fillCircle(x+size/4,y-size/5,size/6,0xffff);
        gfx::line(x-size/2,y+size/3,x+size/2,y-size/3,0);
    }
};
void state(float position,int selected,int opening,float progress,float time) {
    original_launcher::selPos=launcher::selPos=position;
    original_launcher::sel=launcher::sel=selected;
    original_launcher::launching=launcher::launching=opening;
    original_launcher::launchT=launcher::launchT=progress;
    original_launcher::age=launcher::age=time;
}
}
int main() {
    Icon icons[16];Game* games[16];
    for(int i=0;i<16;++i){icons[i].id=i;games[i]=&icons[i];}
    // Guard both ends of the framebuffer, including enlarged launch icons.
    const size_t count=SCR_W*SCR_H;
    std::vector<uint16_t> before(count+4,0xabcd),after(count+4,0xabcd);
    auto guards=[&]() {
        for(auto* pixels:{&before,&after})
            assert((*pixels)[0]==0xabcd && (*pixels)[1]==0xabcd &&
                   (*pixels)[count+2]==0xabcd && (*pixels)[count+3]==0xabcd);
    };
    // Exercise subpixel centres, tiny rings and clipping beyond every edge.
    for(int k=0;k<768;++k) {
        std::fill(before.begin()+2,before.end()-2,0);
        std::fill(after.begin()+2,after.end()-2,0);
        const float x=(k*137%440)-40+(k%7)*.13f;
        const float y=(k*71%440)-40+(k%11)*.09f;
        const float r=1+(k*19%104)+(k%13)*.07f;
        fb=before.data()+2;gfx::strokeCircle(x,y,r,2,0xffff);
        fb=after.data()+2;launcher::strokeRing(x,y,r,2,0xffff);
        if(before!=after){fprintf(stderr,"Ring mismatch: case=%d\n",k);return 1;}
        guards();
    }
    unsigned frames=0;
    for(int n:{0,1,5,6,9,16}) {
        original_launcher::begin(games,n,0);launcher::begin(games,n,0);
        for(int k=0;k<96;++k) {
            const float pos=(k-48)*.173f;
            const int opening=n && k%3==0?k%n:-1;
            state(pos,k-48,opening,(k%24)/24.f,k*.13f);
            fb=before.data()+2;original_launcher::render();
            fb=after.data()+2;launcher::render();
            if(before!=after) {
                for(size_t i=0;i<before.size();++i)if(before[i]!=after[i]) {
                    fprintf(stderr,"Pixel mismatch: apps=%d frame=%d offset=%zu\n",n,k,i);
                    return 1;
                }
            }
            guards();
            ++frames;
        }
    }
    original_launcher::begin(games,9,0);launcher::begin(games,9,0);
    auto bench=[&](bool optimized) {
        fb=after.data()+2;
        const auto start=std::chrono::steady_clock::now();
        for(int k=0;k<1600;++k) {
            state(k*.013f,k/77,-1,0,k*.016f);
            if(optimized)launcher::render();else original_launcher::render();
        }
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    };
    // Alternate order to reduce warmup/order bias; these are host CPU timings.
    double oldMs=0,newMs=0;
    for(int k=0;k<4;++k) {
        if(k%2){newMs+=bench(true);oldMs+=bench(false);}
        else {oldMs+=bench(false);newMs+=bench(true);}
    }
    printf("PASS: 768 clipped/subpixel rings; %u frames identical, 0/1/5/6/9/16 apps, wrap, breathing, launch, framebuffer guards\n",frames);
    printf("Host only: baseline %.3f ms/frame, optimized %.3f ms/frame, render time saved %.1f%%\n",
        oldMs/6400,newMs/6400,100*(oldMs-newMs)/oldMs);
}
