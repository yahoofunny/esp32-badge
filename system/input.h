#pragma once

#include <stdint.h>

// Shared input for the whole system: tilt, touch and the BOOT button. Games and
// the launcher both read from here, so the accelerometer trimming and the touch
// release detection exist in exactly one place.

namespace input {

bool init();

// Call once per frame, before reading anything else. `dt` drives the tilt trim.
void update(float dt);

// --- tilt -------------------------------------------------------------------
// Gravity-referenced tilt in g. Level reads zero: with a gravity-sensed tilt
// control there is no calibration snapshot that could go stale. A slow auto-trim
// removes the accelerometer's static offset, but only while the board is already
// near level, so it never fights a deliberate tilt.
float tiltX();
float tiltY();

// The raw accelerometer vector, in g, with no trimming or deadzone. Tilt control wants
// the processed values above; anything detecting *motion* rather than orientation needs
// these, because the magnitude of this vector is 1 at rest and spikes when shaken.
void readRawAccel(float out[3]);

// --- touch ------------------------------------------------------------------
bool touchDown();
bool tapped();                  // true for exactly one frame, on release
int  tapX();
int  tapY();
unsigned long touchHeldMs();    // 0 while not touching

// --- BOOT button (GPIO0) ----------------------------------------------------
bool bootDown();
unsigned long bootHeldMs();     // 0 while released

}  // namespace input
