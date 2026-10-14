#pragma once

#include <stdint.h>

// The time: the PCF85063 on the I2C bus, and the system clock.
//
// No Wi-Fi. The RTC keeps running off the board's battery, so it only has to be set once;
// after that it is the timekeeper. It arrives set by the host at flashing time over the
// serial port -- see the 'T' command in system.ino -- which means this firmware never
// brings up a radio at all. (The assistant in the other slot has its own Wi-Fi, for its
// own cloud. Separate firmwares, separate radios, separate reasons to have one.)
namespace sysclock {

// Reads the RTC and, if it holds a valid time, sets the system clock from it. Reports
// what it found on the serial line, which is the only way to see it without the screen.
void begin();

// Runs the sync state machine. Called once per frame and never blocks. Does nothing at
// all once the time is known and the RTC agrees.
void update(uint32_t nowMs);

bool valid();               // is the time we have trustworthy
const char *timeText();     // "21:44", or "--:--" when the time is not known
const char *sourceText();   // "rtc", "wifi", or "not set" -- what the time came from

// Writes the RTC from the system clock. This is what a time set over the serial port
// ends up calling, and what the settings page's tap does.
void writeFromSystem();

}  // namespace sysclock
