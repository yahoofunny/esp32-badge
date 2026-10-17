#pragma once
#include <stdint.h>

namespace power {
enum class ChargeState { Unknown, Charging, Full, Discharging, Idle, Paused, NoBattery };
struct Reading {
    // A USB host sends SOF packets even with its serial monitor closed. False
    // does not rule out a charger/power bank, which sends no USB data packets.
    bool usbHostConnected = false;
    bool valid = false;
    int percent = -1;
    int millivolts = 0;
    int milliamps = 0;
    uint16_t batteryStatus = 0;
    ChargeState state = ChargeState::Unknown;
};

// BQ27220 standard commands, not the unrelated BQ27xxx register map.
// Current is signed mA: positive enters the battery, negative leaves it.
template<class Read16> Reading sample(Read16 read, bool usbHostConnected = false) {
    Reading r;
    r.usbHostConnected = usbHostConnected;
    uint16_t mv = 0, soc = 0, flags = 0, current = 0;
    if (!read(0x08, &mv) || !read(0x2c, &soc) || mv < 2500 || mv > 4400 || soc > 100)
        return r;
    r.valid = true; r.percent = soc; r.millivolts = mv;
    if (!read(0x0a, &flags) || !read(0x0c, &current)) return r;
    r.batteryStatus = flags;
    r.milliamps = int16_t(current);
    if (r.milliamps < -2000 || r.milliamps > 2000) return r;
    if (!(flags & 0x0008)) r.state = ChargeState::NoBattery;
    else if (r.milliamps > 5) r.state = ChargeState::Charging;
    else if (r.milliamps < -5) r.state = ChargeState::Discharging;
    // DSG is the gauge's mode flag, with its own exit delay. It neither reports
    // USB presence nor overrides a new positive/zero instantaneous current.
    else if (flags & 0x0900) r.state = ChargeState::Paused; // CHGINH or OTC
    else if (flags & 0x0200) r.state = ChargeState::Full;
    else r.state = ChargeState::Idle;
    return r;
}
inline const char* stateText(ChargeState state) {
    switch (state) {
    case ChargeState::Charging: return "CHARGING";
    case ChargeState::Full: return "FULL";
    case ChargeState::Discharging: return "DISCHARGING";
    case ChargeState::Idle: return "CHARGE UNKNOWN";
    case ChargeState::Paused: return "CHARGE PAUSED";
    case ChargeState::NoBattery: return "NO BATTERY";
    default: return "STATUS UNKNOWN";
    }
}
}
