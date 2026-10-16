// Exercises the actual launcher state machine, including tap/drag disambiguation.
#include <cassert>
#include <cstdio>
#include <cmath>
#include "../system/launcher.cpp"

namespace {
bool finger=false,released=false;int px=180,py=114;float lean=0;
class Dummy:public Game{
    const char* name()const override{return "APP";}
    uint16_t accent()const override{return 0xffff;}
    void update(float)override{} void render()override{}
} apps[6];
Game* entries[]={&apps[0],&apps[1],&apps[2],&apps[3],&apps[4],&apps[5]};
void point(float angle){px=int(lroundf(180+112*cosf(angle)));py=int(lroundf(180+66*sinf(angle)));}
void reset(){finger=released=false;lean=0;launcher::begin(entries,6);}
void hold(float angle){finger=true;released=false;point(angle);assert(!launcher::update(.016f));}
void lift(){finger=false;released=true;assert(!launcher::update(.016f));released=false;}
}
namespace input{
bool touchDown(){return finger;}bool tapped(){return released;}
int tapX(){return px;}int tapY(){return py;}float tiltX(){return lean;}
}
namespace sound {void play2(int,int,int,int){}void launch(){}}
int main(){
    reset();hold(-PI_F/2);
    for(int i=1;i<=12;++i)hold(-PI_F/2+i*(PI_F/3)/12);
    assert(launcher::touchDragging && fabsf(launcher::selPos+1)<.03f);
    lift();assert(launcher::sel==-1 && launcher::launching<0);
    for(int i=0;i<60;++i)assert(!launcher::update(.016f));
    assert(fabsf(launcher::selPos+1)<.001f);
    // A stationary tap still launches after the existing launch animation.
    reset();hold(-PI_F/2);lift();Game* picked=nullptr;
    for(int i=0;i<40 && !picked;++i)picked=launcher::update(.016f);
    assert(picked==&apps[0]);
    // Wrap through +/-pi follows the short arc, not an almost-full revolution.
    reset();hold(170*PI_F/180);hold(179*PI_F/180);hold(-170*PI_F/180);
    assert(launcher::selPos<0 && launcher::selPos>-.5f);lift();
    // Press/release jitter must remain a tap.
    reset();hold(-PI_F/2);px+=2;launcher::update(.016f);assert(!launcher::touchDragging);
    // Crossing the middle must not introduce a sudden half-turn.
    reset();hold(-PI_F/2);hold(-PI_F/3);float before=launcher::selPos;
    px=py=180;launcher::update(.016f);hold(PI_F/3);
    assert(fabsf(launcher::selPos-before)<.001f);lift();
    // Tilt noise cannot fight a finger or launch something on release.
    reset();lean=1;hold(-PI_F/2);hold(-PI_F/3);lift();
    const int selected=launcher::sel;
    for(int i=0;i<10;++i)assert(!launcher::update(.016f));
    assert(launcher::sel==selected);
    launcher::begin(nullptr,0);assert(!launcher::update(.016f));
    puts("PASS: ring drag, snap, tap, angle wrap, centre crossing, tilt suppression, empty registry");
}
