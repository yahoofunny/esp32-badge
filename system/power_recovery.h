#pragma once
#include "power_status.h"

namespace power {
// SOF gaps shorter than half a second can occur under load; do not treat them as
// a physical cable removal and repeatedly start a power-recovery cycle.
class UsbHostLink {
public:
    bool update(bool raw, uint32_t now) {
        if (raw == value_) { pending_ = false; return value_; }
        if (!pending_ || candidate_ != raw) {
            pending_ = true; candidate_ = raw; since_ = now;
        }
        if (uint32_t(now - since_) >= (raw ? 100u : 500u)) {
            value_ = raw; pending_ = false;
        }
        return value_;
    }
private:
    bool value_ = false, pending_ = false, candidate_ = false;
    uint32_t since_ = 0;
};

class RecoveryCheck {
public:
    bool take(const Reading& r, uint32_t now) {
        if (!r.usbHostConnected) { attempted_ = false; waiting_ = false; return false; }
        if (attempted_) return false;
        if (!r.valid || r.state != ChargeState::Idle || !(r.batteryStatus & 0x1000)) {
            waiting_ = false; return false;
        }
        if (!waiting_) { since_ = now; waiting_ = true; }
        if (uint32_t(now - since_) < 2000) return false;
        attempted_ = true;
        return true;
    }
private:
    bool attempted_ = false, waiting_ = false;
    uint32_t since_ = 0;
};
}
