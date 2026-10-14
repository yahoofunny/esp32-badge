#include "clock.h"

#include <Arduino.h>
#include <Wire.h>
#include <time.h>

// The PCF85063 at 0x51, a register at a time over the bus that is already up.
//
// SensorLib has a driver for this chip and it was the first thing tried; its begin()
// re-initialises the I2C bus, and the very next SPI transfer to the LCD failed and left
// the panel driver waiting for a callback that never came -- the frozen splash screen.
// Seven registers and a BCD conversion each is cheaper than finding out why.
//
// No Wi-Fi here at all. The RTC runs off the board's battery, so it only needs setting
// once, and it is set from the host over the serial port ("T<epoch>" -- see system.ino).
// The assistant in the other slot has its own Wi-Fi for its own cloud; separate
// firmwares, separate radios.

static const uint8_t RTC_ADDR = 0x51;
static const uint8_t RTC_REG_SECONDS = 0x04;   // bit 7 is the oscillator-stop flag

static bool rtcOk = false;
static bool timeValid = false;
static const char *sourceName = "not set";

static uint8_t bcd2dec(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static uint8_t dec2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

// The seven time registers, 0x04..0x0A. Returns false if the chip did not answer.
static bool rtcRead(uint8_t *out7, bool *integrityOk)
{
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(RTC_REG_SECONDS);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)RTC_ADDR, (uint8_t)7) != 7) return false;
    for (int i = 0; i < 7; i++) out7[i] = (uint8_t)Wire.read();
    *integrityOk = (out7[0] & 0x80) == 0;      // the oscillator-stop flag
    return true;
}

static bool rtcWrite(const struct tm &t)
{
    Wire.beginTransmission(RTC_ADDR);
    Wire.write(RTC_REG_SECONDS);
    Wire.write(dec2bcd((uint8_t)t.tm_sec) & 0x7F);   // writing seconds clears the OS flag
    Wire.write(dec2bcd((uint8_t)t.tm_min));
    Wire.write(dec2bcd((uint8_t)t.tm_hour));
    Wire.write(dec2bcd((uint8_t)t.tm_mday));
    Wire.write(dec2bcd((uint8_t)(t.tm_wday + 1)));
    Wire.write(dec2bcd((uint8_t)(t.tm_mon + 1)));
    Wire.write(dec2bcd((uint8_t)((t.tm_year + 1900) % 100)));
    return Wire.endTransmission() == 0;
}

// A plausible wall clock. Anything else -- the chip's power-on default, a stopped
// oscillator -- is not worth pretending about.
static bool plausible(int year) { return year >= 2024 && year <= 2099; }

namespace sysclock {

void begin()
{
    // UTC+8, in the POSIX form where the sign is inverted: TZ="CST-8" means eight hours
    // ahead of UTC. Without this the default is UTC and every localtime() call is eight
    // hours out -- which is exactly how the first RTC write here lost eight hours.
    setenv("TZ", "CST-8", 1);
    tzset();

    uint8_t r[7] = {0};
    bool integrity = false;
    if (!rtcRead(r, &integrity)) {
        Serial.println("[clock] no RTC on the bus");
        return;
    }
    rtcOk = true;

    const int year = 2000 + bcd2dec(r[6]);
    Serial.printf("[clock] rtc %04d-%02d-%02d %02d:%02d:%02d, %s\n",
                  year, bcd2dec(r[5] & 0x1F), bcd2dec(r[3] & 0x3F),
                  bcd2dec(r[2] & 0x3F), bcd2dec(r[1] & 0x7F), bcd2dec(r[0] & 0x7F),
                  integrity ? "running since it was last set" : "oscillator stopped");

    if (!integrity || !plausible(year)) {
        Serial.println("[clock] RTC time is not usable; a host can set it with T<epoch>");
        return;
    }

    // System clock from the RTC. mktime reads a struct tm as local time and everything
    // here is local, so nothing is offset twice. (timegm would be right if the RTC held
    // UTC; it does not.)
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = bcd2dec(r[5] & 0x1F) - 1;
    t.tm_mday = bcd2dec(r[3] & 0x3F);
    t.tm_hour = bcd2dec(r[2] & 0x3F);
    t.tm_min = bcd2dec(r[1] & 0x7F);
    t.tm_sec = bcd2dec(r[0] & 0x7F);
    const time_t epoch = mktime(&t);
    struct timeval tv = { epoch, 0 };
    settimeofday(&tv, nullptr);
    timeValid = true;
    sourceName = "rtc";
    Serial.println("[clock] system clock set from the RTC");
}

void writeFromSystem()
{
    if (!rtcOk) {
        Serial.println("[clock] no RTC to write to");
        return;
    }
    const time_t now = time(nullptr);
    if (now < 1700000000) {
        Serial.println("[clock] system time is not real; nothing written to the RTC");
        return;
    }
    struct tm t;
    localtime_r(&now, &t);
    if (rtcWrite(t)) {
        timeValid = true;
        sourceName = "set";
        Serial.printf("[clock] RTC set to %04d-%02d-%02d %02d:%02d:%02d\n",
                      t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
                      t.tm_sec);
    } else {
        Serial.println("[clock] writing the RTC failed");
    }
}

void update(uint32_t nowMs)
{
    // Nothing to do. There used to be a sync state machine here, over Wi-Fi; the time now
    // comes from the RTC, which the host sets once over the serial port.
    (void)nowMs;
}

bool valid() { return timeValid; }

const char *timeText()
{
    static char buf[8];
    if (!timeValid) {
        snprintf(buf, sizeof(buf), "--:--");
        return buf;
    }
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    return buf;
}

const char *sourceText() { return sourceName; }

}  // namespace sysclock
