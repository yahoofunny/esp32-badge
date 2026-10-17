#pragma once

#include <stdint.h>
#include "power_status.h"

// The battery, which on this board is a BQ27220 fuel gauge on the shared I2C bus at
// 0x55. It is the only thing here that knows how much charge is left: there is no
// voltage divider on an ADC pin to read instead.
//
// Voltage/SOC plus BQ27220 BatteryStatus and signed Current distinguish actual
// battery charging from merely having a USB cable connected.
namespace power {

// Called from the frame loop. The gauge is sampled at most every couple of seconds
// rather than per frame: the bus is shared with the touch controller and the IMU, and
// the charge level does not change faster than that.
void poll(uint32_t nowMs);

// The last sample. Cheap -- no I2C.
Reading cached();
void requestChargeOnly();
bool chargeOnlyRequested();

}  // namespace power
