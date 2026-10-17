#pragma once
#include <algorithm>
#include <cmath>

namespace hypno {
// A damped pendulum driven by changes in measured support acceleration.
// Removing the slowly changing gravity vector permits any holding orientation;
// a still device cannot inject energy. There is no periodic animation driver.
class Motion {
public:
    float angle=0,velocity=0,driveX=0,driveY=0,raw[3]={};
    bool sensorValid=false;

    void reset() { *this=Motion(); }
    void step(float dt,const float accel[3],bool valid,float gain,bool paused=false) {
        if(!std::isfinite(dt)||dt<=0)return;
        dt=std::min(dt,.1f);
        valid=valid&&accel&&std::isfinite(accel[0])&&std::isfinite(accel[1])&&
            std::isfinite(accel[2])&&
            accel[0]*accel[0]+accel[1]*accel[1]+accel[2]*accel[2]>.01f;
        sensorValid=valid;
        driveX=driveY=0;
        if(valid) {
            for(int i=0;i<3;++i)raw[i]=std::clamp(accel[i],-4.0f,4.0f);
            if(!calibrated||paused) {
                for(int i=0;i<3;++i)baseline[i]=raw[i];
                calibrated=true;
            } else {
                // Screen X is -raw X on this board. A hanging mass responds
                // opposite to its support's acceleration, giving +raw X here.
                driveX=deadzone(raw[0]-baseline[0]);
                driveY=deadzone(-(raw[1]-baseline[1]));
                const float alpha=dt/(.65f+dt);
                for(int i=0;i<3;++i)baseline[i]+=(raw[i]-baseline[i])*alpha;
            }
        }
        if(paused)return; // Continue sampling; no stored shove on resume.
        gain=std::isfinite(gain)?std::clamp(gain,0.0f,6.0f):0;
        // Small integration steps keep the response stable at variable UI rates.
        float remaining=dt;
        while(remaining>0) {
            const float h=std::min(remaining,1.0f/120.0f);
            constexpr float spring=3.42f,damping=.75f; // ~3.4 s free swing
            const float force=driveX*std::cos(angle)-driveY*std::sin(angle);
            const float acceleration=spring*(-std::sin(angle)+gain*force)-damping*velocity;
            velocity=std::clamp(velocity+acceleration*h,-4.0f,4.0f);
            angle+=velocity*h;
            if(angle>.82f){angle=.82f;if(velocity>0)velocity*=-.15f;}
            if(angle<-.82f){angle=-.82f;if(velocity<0)velocity*=-.15f;}
            remaining-=h;
        }
        if(std::fabs(angle)<.0015f&&std::fabs(velocity)<.006f&&
           driveX==0&&driveY==0)angle=velocity=0;
    }
private:
    float baseline[3]={};
    bool calibrated=false;
    static float deadzone(float v) {
        return std::fabs(v)<.018f?0.0f:std::clamp(v,-1.8f,1.8f);
    }
};
} // namespace hypno
