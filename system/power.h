#pragma once

#include <stdint.h>

// The battery, which on this board is a BQ27220 fuel gauge on the shared I2C bus at
// 0x55. It is the only thing here that knows how much charge is left: there is no
// voltage divider on an ADC pin to read instead.
//
// Two of its standard commands are used and both were checked against the device:
// 0x08 returns the pack voltage in millivolts and 0x2C the state of charge in percent.
// The temperature and current commands read as nonsense on this gauge, so they are left
// alone rather than shown with a made-up scale.
namespace power {

struct Reading {
    bool valid = false;     // did the gauge answer and look plausible
    int  percent = -1;      // state of charge, 0..100
    int  millivolts = 0;    // pack voltage
};

// Called from the frame loop. The gauge is sampled at most every couple of seconds
// rather than per frame: the bus is shared with the touch controller and the IMU, and
// the charge level does not change faster than that.
void poll(uint32_t nowMs);

// The last sample. Cheap -- no I2C.
Reading cached();

}  // namespace power
