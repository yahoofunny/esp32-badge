# Work in progress, not in the build

Files here are **not** compiled: arduino-cli only builds `system/`, so anything parked here
is inert. That is the point -- a half-finished app left inside the sketch folder either
breaks the build or, worse, compiles and looks like it works.

## game_music.{h,cpp}

The music player for `/Music`. Written, does not compile yet.

State: `#include "Audio.h"` resolves but `class Audio` does not become visible -- no
"file not found", so the header is being found and something inside it is not reaching the
class. Next things to try, in order:

1. `arduino-cli compile --verbose` and look at the include paths it actually passes, to see
   which `Audio.h` is winning.
2. The manual install is `ESP32-audioI2S-master` in the sketchbook; the registry version is
   named differently. Try installing from the registry and letting it manage the folder
   name.
3. Failing that, include the header by its library-qualified form, or drop the library and
   drive the decoder through ESP-IDF's `esp_audio_codec`.

Design notes that are worth keeping whatever the wrapper turns out to be:

* **It is a mode, entered by rebooting.** The library installs its own I2S on the same pins
  this system's sound effects use, and the Arduino `I2SClass` has no `end()` -- there is no
  way to hand the peripheral over. In player mode `setup()` never starts the sound effects'
  I2S at all. An NVS flag means "boot into the player once" and is cleared as the player
  starts, so a crash costs one ordinary boot rather than a loop.
* `Audio` is constructed on the heap, not as a global. A global constructor runs before
  `setup()`, and exactly that was enough to make the LCD's first SPI transfer fail when the
  image decoders were linked that way -- see commit c9c593b.
* The library has a destructor (`~Audio()`), so if it does release the I2S cleanly then the
  reboot on exit could be dropped. Untested.
