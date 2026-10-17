#include <cassert>
#include <cstdio>
#include <cstring>
#include "../system/power_status.h"

static power::Reading reading(int current,uint16_t status=0x4028,int percent=84,int failed=-1,bool usb=false) {
    return power::sample([&](uint8_t reg,uint16_t* out) {
        if (reg==failed) return false;
        switch(reg) {
        case 0x08:*out=4040;break;case 0x2c:*out=percent;break;
        case 0x0a:*out=status;break;case 0x0c:*out=uint16_t(int16_t(current));break;
        default:assert(false);
        }return true;
    },usb);
}
int main() {
    using S=power::ChargeState;
    assert(reading(180).state==S::Charging);
    assert(reading(-140,0x4029).state==S::Discharging);
    // Replugging can change current before DSG clears. Neither is a USB detector.
    assert(reading(180,0x4029,84,-1,true).state==S::Charging);
    assert(reading(0,0x4029,84,-1,true).state==S::Idle);
    assert(reading(-140,0x4029,84,-1,true).state==S::Discharging);
    assert(reading(-140,0x4029,84,-1,true).usbHostConnected);
    assert(reading(0,0x5028,10,-1,true).usbHostConnected);
    assert(reading(0,0x5028,10,-1,true).state==S::Idle);
    assert(reading(0,0x4028,84,0x08,true).usbHostConnected);
    assert(!reading(180).usbHostConnected); // Charger may have no USB host.
    assert(reading(180).state==S::Charging);
    assert(!std::strstr(power::stateText(S::Discharging),"ON BATTERY"));
    assert(reading(0,0x4228,100).state==S::Full);
    assert(reading(0,0x4028,100).state==S::Idle); // 100% alone does not mean charging completed.
    assert(reading(0,0x4128).state==S::Paused);
    assert(reading(0,0x4828).state==S::Paused);
    assert(reading(0,0x4020).state==S::NoBattery);
    assert(reading(32767).state==S::Unknown);
    assert(reading(-32768).state==S::Unknown);
    assert(reading(100,0x4028,84,0x0c).state==S::Unknown);
    assert(reading(100,0x4028,84,0x0a).state==S::Unknown);
    assert(reading(100,0x4028,84,0x0c).valid);
    assert(!reading(100,0x4028,84,0x08).valid);
    assert(!reading(100,0x4028,101).valid);
    puts("PASS: BQ27220 signed charging/discharging current, full vs 100%, paused/absent, failed reads");
}
