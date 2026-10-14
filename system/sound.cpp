#include "sound.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

#include "audio.h"
#include "es8311.h"

// The pins are the board's, from docs/hardware.md. GPIO9 is the amplifier enable: the
// codec will happily convert samples with the amp off and nothing comes out.
static const int PIN_MCK = 2, PIN_BCK = 48, PIN_LRCK = 38, PIN_DOUT = 47, PIN_DIN = 39;
static const int PIN_PA = 9;
static const int RATE = 24000;          // what the vendor example uses, and what the
                                        // assistant uses, so the codec's clock is known good
static const int SAMPLES_PER_UPDATE = 480;   // 20 ms; the I2S write blocks when the DMA
                                             // is full, so this self-regulates the rate

static I2SClass i2s;
static es8311_handle_t codec = nullptr;
static bool ready = false;

// --- the voice ------------------------------------------------------------------
// A square wave with a pitch and an amplitude that decay. That is what an eight-bit
// machine had: no wavetables, no mixer, no sample rate conversion.
static uint32_t phase = 0;
static uint32_t step = 0;               // phase advance per sample, for the current note
static int      msLeft = 0;
static int      msTotal = 1;
static uint32_t nextStep = 0;           // the second half of a two-note call
static int      nextMs = 0;

// Written sample by sample. Silence when nothing is sounding, because the I2S keeps
// clocking whether we feed it or not.
static void fill(int16_t *out, int count)
{
    for (int i = 0; i < count; i++) {
        if (msLeft <= 0) {
            if (nextMs > 0) {                      // start the second note
                step = nextStep;
                msLeft = msTotal = nextMs;
                nextMs = 0;
            } else {
                out[i] = 0;
                continue;
            }
        }
        phase += step;
        // Attack over the first 2 ms so the note does not click, then decay: a pluck,
        // which is what makes a square wave sound like a chip rather than a buzzer.
        const int elapsed = msTotal - msLeft;
        float env = 1.0f;
        if (elapsed < 2) env = elapsed * 0.5f;
        else env = (float)msLeft / (float)msTotal;
        // -3 dBFS at the peak of the envelope. The codec's own level is the volume
        // control; a synth that also runs quietly just makes everything quieter.
        const int amp = (int)(env * 23000.0f);
        out[i] = (int16_t)((phase & 0x80000000u) ? amp : -amp);
        msLeft--;
    }
}

static uint32_t stepFor(int hz)
{
    if (hz < 20) hz = 20;
    return (uint32_t)((double)hz * 4294967296.0 / (double)RATE);
}

namespace sound {

bool begin()
{
    // Codec first, then the clocks. This is the order the vendor example uses, and the
    // codec's divisors are computed from the configured rate rather than measured, so
    // nothing depends on the order -- but it is one fewer difference from a path that is
    // known to work on this board.
    codec = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
    if (!codec) {
        Serial.println("[sound] codec handle failed");
        return false;
    }
    const es8311_clock_config_t clk = { false, false, true, RATE * 256, RATE };
    if (es8311_init(codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        Serial.println("[sound] codec did not initialise");
        return false;
    }

    i2s.setPins(PIN_BCK, PIN_LRCK, PIN_DOUT, PIN_DIN, PIN_MCK);
    if (!i2s.begin(I2S_MODE_STD, RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO,
                   I2S_STD_SLOT_LEFT)) {
        Serial.println("[sound] i2s did not start");
        return false;
    }

    // The level the settings page holds. Same percentage scale the driver uses, which is
    // why the two agree: this is the register audio.cpp also writes directly.
    int vol = audio::volume();
    if (vol < 0) vol = 60;
    es8311_voice_volume_set(codec, vol, nullptr);

    pinMode(PIN_PA, OUTPUT);
    digitalWrite(PIN_PA, HIGH);          // the amplifier

    ready = true;
    Serial.printf("[sound] up: %d Hz, volume %d%%, amp on\n", RATE, vol);
    return true;
}

void play(int hz, int ms)
{
    if (!ready || ms <= 0) return;
    step = stepFor(hz);
    msLeft = msTotal = ms;
    nextMs = 0;
}

void play2(int hz1, int ms1, int hz2, int ms2)
{
    if (!ready) return;
    step = stepFor(hz1);
    msLeft = msTotal = ms1 > 0 ? ms1 : 1;
    nextStep = stepFor(hz2);
    nextMs = ms2;
}

void update()
{
    if (!ready) return;
    static int16_t buf[SAMPLES_PER_UPDATE];
    fill(buf, SAMPLES_PER_UPDATE);
    i2s.write((uint8_t *)buf, SAMPLES_PER_UPDATE * sizeof(int16_t));
}

void stop()
{
    if (!ready) return;
    msLeft = 0;
    nextMs = 0;
    digitalWrite(PIN_PA, LOW);
    ready = false;
}

// --- the system's sounds --------------------------------------------------------
// Short, and few. A badge that chirps at everything is a nuisance.
void click()  { play(1180, 28); }
void launch() { play2(620, 45, 930, 60); }
void back()   { play2(930, 45, 620, 60); }
void ok()     { play2(1046, 50, 1568, 80); }
void nope()   { play2(400, 90, 290, 120); }
void nudge(int percent)
{
    // Pitch follows the value, so the sound is also a readout of where the level is.
    int hz = 500 + percent * 9;
    play(hz, 40);
}

}  // namespace sound
