#include "power.h"

#include <Arduino.h>
#include <Wire.h>

namespace power {

constexpr uint8_t ADDR = 0x55;
constexpr uint8_t CMD_VOLTAGE = 0x08;   // millivolts, little-endian
constexpr uint8_t CMD_SOC = 0x2C;       // percent, little-endian
constexpr uint32_t POLL_MS = 2000;

// A plausible single-cell pack. Anything outside this is a failed read that happened to
// return two bytes, which is worth rejecting: a gauge that has been unplugged reads as
// zeros, and "0%" on screen would be a lie rather than a missing battery.
constexpr int MV_MIN = 2500;
constexpr int MV_MAX = 4400;

static Reading last;
static uint32_t lastPoll = 0;
static bool everPolled = false;

static bool read16(uint8_t cmd, uint16_t *out)
{
    Wire.beginTransmission(ADDR);
    Wire.write(cmd);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)ADDR, (uint8_t)2) != 2) return false;
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    *out = (uint16_t)(lo | ((uint16_t)hi << 8));
    return true;
}

static Reading sample()
{
    Reading r;
    uint16_t mv = 0, soc = 0;
    const bool okV = read16(CMD_VOLTAGE, &mv);
    const bool okS = read16(CMD_SOC, &soc);
    if (!okV || !okS) return r;
    if (mv < MV_MIN || mv > MV_MAX || soc > 100) return r;
    r.valid = true;
    r.millivolts = (int)mv;
    r.percent = (int)soc;
    return r;
}

void poll(uint32_t nowMs)
{
    // millis() wraps, and so does this comparison -- that is why it is written as a
    // subtraction rather than nowMs > lastPoll.
    if (everPolled && (uint32_t)(nowMs - lastPoll) < POLL_MS) return;
    everPolled = true;
    lastPoll = nowMs;
    const Reading r = sample();
    // Keep the last good reading rather than replacing it with a failure: the bus is
    // shared, so a single dropped transaction says nothing about the battery.
    if (r.valid) last = r;
    else if (!last.valid) last = r;
}

Reading cached()
{
    return last;
}

}  // namespace power
