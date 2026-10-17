#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "../system/power_recovery.h"

static power::Reading stalled() {
    power::Reading r;r.usbHostConnected=true;r.valid=true;
    r.state=power::ChargeState::Idle;r.batteryStatus=0x5028;return r;
}
int main() {
    using S=power::ChargeState;
    power::UsbHostLink link;
    assert(!link.update(true,0));assert(!link.update(true,99));assert(link.update(true,100));
    assert(link.update(false,200));assert(link.update(false,699));assert(link.update(true,700));
    assert(link.update(false,1000));assert(!link.update(false,1500));
    assert(!link.update(true,1600));assert(link.update(true,1700));
    power::UsbHostLink wrap;
    assert(!wrap.update(true,0xfffffff0));assert(wrap.update(true,0x54));
    power::RecoveryCheck recovery;
    auto r=stalled();
    assert(!recovery.take(r,0));assert(!recovery.take(r,1999));assert(recovery.take(r,2000));
    for (uint32_t t=2001;t<20000;t+=100) assert(!recovery.take(r,t));
    r.usbHostConnected=false;assert(!recovery.take(r,20000));
    r.usbHostConnected=true;assert(!recovery.take(r,21000));assert(recovery.take(r,23000));
    for (S state : {S::Charging,S::Discharging,S::Full,S::Paused,S::NoBattery,S::Unknown}) {
        power::RecoveryCheck check;r=stalled();r.state=state;
        assert(!check.take(r,0));assert(!check.take(r,30000));
    }
    power::RecoveryCheck fresh;
    r=stalled();r.batteryStatus=0x4028;
    assert(!fresh.take(r,0));assert(!fresh.take(r,30000));
    r=stalled();assert(!fresh.take(r,40000));r.valid=false;assert(!fresh.take(r,42000));
    r.valid=true;assert(!fresh.take(r,43000));assert(fresh.take(r,45000));
    power::RecoveryCheck timerWrap;r=stalled();
    assert(!timerWrap.take(r,0xffffff00));assert(timerWrap.take(r,0x6d0));
    puts("PASS: USB debounce, SOF gaps, bounded single recovery per cable session, charge/full/failure guards, timer wrap");
}
