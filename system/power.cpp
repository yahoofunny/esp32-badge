#include "power.h"
#include "power_recovery.h"
#include "gauge_capacity.h"

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include "driver/usb_serial_jtag.h"

namespace power {

constexpr uint8_t ADDR = 0x55;
constexpr uint32_t POLL_MS = 1000;

static Reading last;
static uint32_t lastPoll = 0;
static bool everPolled = false;
static bool sleepRequested = false;
static uint32_t usbConnects = 0, usbDisconnects = 0;
static UsbHostLink usbLink;
static RecoveryCheck recovery;

static bool read16(uint8_t cmd, uint16_t *out)
{
    // BQ27220 TRM 5.3 requires >=66 us between packets at 400 kHz.
    // Keep the shared touch/IMU bus speed, but give this device explicit STOP
    // boundaries and 100 us bus-free time before issuing its quick read.
    delayMicroseconds(100);
    Wire.beginTransmission(ADDR);
    Wire.write(cmd);
    if (Wire.endTransmission(true) != 0) return false;
    delayMicroseconds(100);
    if (Wire.requestFrom((uint8_t)ADDR, (uint8_t)2) != 2) return false;
    const uint8_t lo = Wire.read();
    const uint8_t hi = Wire.read();
    *out = (uint16_t)(lo | ((uint16_t)hi << 8));
    delayMicroseconds(100);
    return true;
}

class CapacityIo final : public GaugeConfigIo {
public:
    bool read(uint8_t reg, uint8_t *data, uint8_t size) override {
        delayMicroseconds(100);
        Wire.beginTransmission(ADDR); Wire.write(reg);
        if (Wire.endTransmission(true) != 0) return false;
        delayMicroseconds(100);
        if (Wire.requestFrom(ADDR,size) != size) return false;
        for (uint8_t i=0;i<size;++i) data[i]=Wire.read();
        delayMicroseconds(100); return true;
    }
    bool write(uint8_t reg, const uint8_t *data, uint8_t size) override {
        delayMicroseconds(100);
        Wire.beginTransmission(ADDR); Wire.write(reg); Wire.write(data,size);
        const bool ok=Wire.endTransmission(true)==0;
        delayMicroseconds(100); return ok;
    }
    void wait(uint32_t ms) override { delay(ms); }
    bool backup(const uint8_t *data, uint8_t size) override {
        Serial.print("[capacity] original 929d=");
        for (uint8_t i=0;i<size;++i) Serial.printf("%02x",data[i]);
        Serial.println(); Serial.flush();
        Preferences prefs;
        if (!prefs.begin("battery",false)) return false;
        bool ok=true;
        if (!prefs.isKey("original")) ok=prefs.putBytes("original",data,size)==size;
        uint8_t saved[32];
        if (ok) ok=prefs.getBytes("original",saved,sizeof saved)==size;
        prefs.end(); return ok;
    }
};

static bool applyCapacity(uint16_t mah) {
    CapacityIo io;
    const CapacityResult result=configureGaugeCapacity(io,mah);
    Serial.printf("[capacity] target=%umAh result=%u (0=unchanged,1=verified)\n",mah,unsigned(result));
    everPolled=false;
    poll(millis());
    return result==CapacityResult::Unchanged || result==CapacityResult::Updated;
}

void restoreBatteryCapacity() {
    Preferences prefs;
    if (!prefs.begin("battery",true)) return;
    const uint16_t mah=prefs.getUShort("capacity",0);
    prefs.end();
    if (mah>=100 && mah<=10000) applyCapacity(mah);
}

bool configureBattery500() {
    if (!applyCapacity(500)) return false;
    Preferences prefs;
    if (!prefs.begin("battery",false)) return false;
    const bool ok=prefs.putUShort("capacity",500)==sizeof(uint16_t) &&
        prefs.getUShort("capacity",0)==500;
    prefs.end();
    Serial.printf("[capacity] saved=%d\n",ok);
    return ok;
}

static Reading sample(bool usbHostConnected)
{
    return power::sample(read16, usbHostConnected);
}

void poll(uint32_t nowMs)
{
    // USB presence is independent of the battery gauge and must update even
    // between gauge polls, or when that I2C device is unavailable.
    const bool usb = usbLink.update(usb_serial_jtag_is_connected(),nowMs);
    if (usb != last.usbHostConnected) {
        if (usb) ++usbConnects; else ++usbDisconnects;
    }
    last.usbHostConnected = usb;
    // millis() wraps, and so does this comparison -- that is why it is written as a
    // subtraction rather than nowMs > lastPoll.
    if (everPolled && (uint32_t)(nowMs - lastPoll) < POLL_MS) return;
    everPolled = true;
    lastPoll = nowMs;
    const Reading r = sample(usb);
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
bool takeRecoveryRequest(uint32_t nowMs) { return recovery.take(last,nowMs); }
void printDiagnostics() {
    Serial.printf("[power] USB transitions connected=%u disconnected=%u\n",usbConnects,usbDisconnects);
    const uint8_t regs[] = {0x06,0x0a,0x0c,0x14,0x10,0x12,0x30,0x3a,0x3c};
    for (uint8_t reg : regs) {
        uint16_t raw = 0;
        if (read16(reg,&raw)) Serial.printf("[gauge] reg=%02x raw=%04x signed=%d\n",reg,raw,int16_t(raw));
        else Serial.printf("[gauge] reg=%02x read failed\n",reg);
    }
}

}  // namespace power
