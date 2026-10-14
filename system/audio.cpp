#include "audio.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>

// The I2C bus is already up by the time any of this runs: I2C_Init() happens before
// LCD_Init() in setup(), because the touch controller needs it. The codec sits on the
// same bus at 0x30 and its register interface does not depend on the audio clocks,
// so these two transactions work whether or not anyone has configured I2S.

namespace audio {

static bool codecWrite(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool codecRead(uint8_t reg, uint8_t *out)
{
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1) != 1) return false;
    *out = (uint8_t)Wire.read();
    return true;
}

// The codec's own mapping, as the vendor driver implements it: 0 is silence and the
// rest is linear over 1..255.
static int percentToRegister(int percent)
{
    if (percent <= 0) return 0;
    if (percent > VOLUME_MAX) percent = VOLUME_MAX;
    return percent * 256 / 100 - 1;
}

static int registerToPercent(int reg)
{
    if (reg <= 0) return 0;
    int pct = (reg + 1) * 100 / 256;
    return pct > VOLUME_MAX ? VOLUME_MAX : pct;
}

int rawRegister()
{
    uint8_t v = 0;
    return codecRead(REG_DAC_VOLUME, &v) ? (int)v : -1;
}

int volume()
{
    const int raw = rawRegister();
    return raw < 0 ? -1 : registerToPercent(raw);
}

bool setVolume(int percent)
{
    return codecWrite(REG_DAC_VOLUME, (uint8_t)percentToRegister(percent));
}

bool verify(int percent)
{
    const int want = percentToRegister(percent);
    for (int attempt = 0; attempt < 3; attempt++) {
        uint8_t got = 0;
        if (codecRead(REG_DAC_VOLUME, &got) && (int)got == want) return true;
        delay(2);
    }
    return false;
}

// Kept in the same NVS partition as everything else, under its own namespace so it
// cannot collide with the pet's save.
static const char *NVS_NAMESPACE = "badge";
static const char *NVS_KEY = "volume";

int storedVolume()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return -1;
    const int v = prefs.isKey(NVS_KEY) ? (int)prefs.getUChar(NVS_KEY, 50) : -1;
    prefs.end();
    return v;
}

void storeVolume(int percent)
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.putUChar(NVS_KEY, (uint8_t)constrain(percent, VOLUME_MIN, VOLUME_MAX));
    prefs.end();
}

void applyStored()
{
    const int v = storedVolume();
    if (v < 0) return;
    setVolume(v);
}

// The assistant's own namespace and key, written exactly as its Settings class reads
// them: nvs_open("audio") and nvs_get_i32("output_volume").
static const char *ASSISTANT_NAMESPACE = "audio";
static const char *ASSISTANT_KEY = "output_volume";

int assistantVolume()
{
    Preferences prefs;
    if (!prefs.begin(ASSISTANT_NAMESPACE, true)) return ASSISTANT_DEFAULT;
    const int v = prefs.getInt(ASSISTANT_KEY, ASSISTANT_DEFAULT);
    prefs.end();
    return v;
}

bool setAssistantVolume(int percent)
{
    if (percent < ASSISTANT_MIN) percent = ASSISTANT_MIN;
    if (percent > VOLUME_MAX) percent = VOLUME_MAX;

    Preferences prefs;
    if (!prefs.begin(ASSISTANT_NAMESPACE, false)) return false;
    const size_t written = prefs.putInt(ASSISTANT_KEY, percent);
    prefs.end();
    if (written == 0) return false;

    // Read it back through the same path the assistant will use. The pet's save
    // taught this lesson: a write that reports success and a value that is not there
    // look identical until something reads it.
    return assistantVolume() == percent;
}

}  // namespace audio
