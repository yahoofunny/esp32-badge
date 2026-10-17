#pragma once

#include <stdint.h>

// Sound: the ES8311 codec, the speaker amp on GPIO9, and a small square/noise synth.
//
// Chiptune rather than files. The badge has a small mono speaker and no headphone jack,
// so a music decoder would be a lot of machinery for a sound that will not be very good
// anyway. What it is good at is blips: two or three oscillators with a pitch envelope and
// a decay is what an eight-bit machine had, it costs a few hundred bytes instead of a
// library, and it matches the pixel art.
//
// Everything here is ours. The assistant rewrites the codec on its own boot -- see
// audio.h -- so this only has to be right while our firmware is the one running.
namespace sound {

// Sets up I2S and the codec, and applies the volume the settings page holds. Safe to call
// once; returns false if the codec did not answer.
bool begin();
// Release I2S and its codec handle while a file player owns the audio hardware.
bool suspend();

// One note, non-blocking. A later call replaces whatever is playing: there is one voice
// and no mixer, which is exactly the constraint an eight-bit machine worked under.
void play(int frequencyHz, int milliseconds);

// Two notes in sequence, which is the smallest thing that can sound like an interval
// rather than a beep.
void play2(int hz1, int ms1, int hz2, int ms2);

// The handful of sounds the system uses, so a caller asks for a meaning rather than a
// pitch. Kept deliberately short -- a badge that chirps at every touch is a nuisance.
void click();       // a tap that did something
void launch();      // opening a screen
void back();        // leaving one
void ok();          // an action that worked
void nope();        // one that did not
void nudge(int step);// settings: the pitch follows the value, so it is also a readout

// Advances the envelope. Call once per frame.
void update();

// Silences everything. Called when the firmware hands the board to the assistant, which
// takes the codec for itself.
void stop();

}  // namespace sound
