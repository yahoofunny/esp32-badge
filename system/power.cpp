#include "power.h"

#include <Arduino.h>
#include <Wire.h>

namespace power {

constexpr uint8_t ADDR = 0x55;
constexpr uint32_t POLL_MS = 2000;

static Reading last;
static uint32_t lastPoll = 0;
static bool everPolled = false;
static bool sleepRequested = false;

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
    return power::sample(read16);
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
    else { last.state = ChargeState::Unknown; if (!last.valid) last = r; }
}

Reading cached()
{
    return last;
}
void requestChargeOnly() { sleepRequested = true; }
bool chargeOnlyRequested() { return sleepRequested; }

}  // namespace power
