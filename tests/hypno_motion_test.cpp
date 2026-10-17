#include "../system/hypno_motion.h"
#include <cassert>
#include <cstdio>
#include <cmath>

static float pulse(float dt,float gain,float sign=1) {
    hypno::Motion m;float a[3]={0,0,1};m.step(dt,a,true,gain);
    float peak=0;
    for(float t=0;t<2.2f;t+=dt) {
        a[0]=t<.18f?.6f*sign:0;m.step(dt,a,true,gain);
        peak=std::fmax(peak,std::fabs(m.angle));
    }
    return peak;
}
int main() {
    hypno::Motion still;float a[3]={.6f,-.2f,.77f};
    for(int i=0;i<6000;++i)still.step(.02f,a,true,2.4f);
    assert(still.sensorValid&&still.angle==0&&still.velocity==0);
    // Static gravity and normal sensor noise must not power an animation.
    for(int i=0;i<2000;++i) {
        a[0]=.6f+std::sin(i*.7f)*.006f;a[1]=-.2f+std::cos(i*.9f)*.006f;
        still.step(.02f,a,true,2.4f);
    }
    assert(still.angle==0&&still.velocity==0);

    hypno::Motion right,left;a[0]=a[1]=0;a[2]=1;
    right.step(.02f,a,true,3.6f);left.step(.02f,a,true,3.6f);
    for(int i=0;i<12;++i) {
        a[0]=.6f;right.step(.02f,a,true,3.6f);
        a[0]=-.6f;left.step(.02f,a,true,3.6f);
    }
    assert(right.angle>.1f&&right.velocity>0&&left.angle<-.1f&&left.velocity<0);
    assert(std::fabs(right.angle+left.angle)<.0001f);
    // A reversing stroke reverses the applied torque, rather than advancing a timer.
    a[0]=-.8f;right.step(.02f,a,true,2.4f);assert(right.driveX<0);
    const float angle=right.angle,velocity=right.velocity;
    for(int i=0;i<200;++i) {
        a[0]=(i&1)?1.5f:-1.5f;right.step(.02f,a,true,2.4f,true);
    }
    assert(right.angle==angle&&right.velocity==velocity);
    // No backlog of shaken-while-paused impulses on resume.
    right.step(.02f,a,true,2.4f);assert(right.driveX==0);
    a[0]=0;
    for(int i=0;i<1500;++i)right.step(.02f,a,true,2.4f);
    assert(right.angle==0&&right.velocity==0);

    assert(pulse(.02f,5.4f)>pulse(.02f,3.6f)&&pulse(.02f,3.6f)>pulse(.02f,1.8f));
    const float reference=pulse(.01f,3.6f);
    for(float dt:{.02f,.05f,1.0f/15})assert(std::fabs(pulse(dt,3.6f)-reference)<.10f);
    // User requested a slower hanging watch: measure two like-direction zero
    // crossings of the undriven pendulum, rather than just checking a constant.
    hypno::Motion slow;a[0]=a[1]=0;a[2]=1;slow.step(.005f,a,true,0);
    slow.angle=.3f;float crossings[2]={};int found=0;
    for(int i=1;i<2400&&found<2;++i) {
        const float before=slow.angle;slow.step(.005f,a,true,0);
        if(before<0&&slow.angle>=0)crossings[found++]=i*.005f;
    }
    assert(found==2&&crossings[1]-crossings[0]>3.3f&&crossings[1]-crossings[0]<3.8f);
    // Extreme and failed measurements never create an unbounded/NaN pendulum.
    for(int i=0;i<20000;++i) {
        a[0]=(i&1)?4:-4;a[1]=(i%3)?4:-4;
        right.step(.1f,a,true,4);
        assert(std::isfinite(right.angle)&&std::fabs(right.angle)<=.82f);
        assert(std::isfinite(right.velocity)&&std::fabs(right.velocity)<=4);
    }
    a[0]=NAN;right.step(.02f,a,true,4);assert(!right.sensorValid);
    right.step(NAN,a,true,4);right.step(INFINITY,a,true,4);right.step(-1,a,true,4);
    assert(std::isfinite(right.angle));
    right.reset();a[0]=a[1]=a[2]=0;
    for(int i=0;i<100;++i)right.step(.02f,a,false,4);
    assert(!right.sensorValid&&right.angle==0&&right.velocity==0);
    std::puts("PASS: sensor-driven directional pendulum, no autonomous/noise motion, damping, pause, sensitivity, UI-rate independence and bounds");
}
