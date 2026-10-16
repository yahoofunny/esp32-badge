#pragma once
#include <stdint.h>

// Small pixel accessories, anchored to the existing body without resizing it.
namespace outfits {
uint8_t count();
const char* name(uint8_t index);
uint32_t duration(uint8_t index);
// Overlays only the accessory. Existing body, eyes, motion and proportions remain
// owned by GamePet. Dimensions must be 1..1024; drawing clips to the framebuffer.
bool draw(uint8_t index, uint32_t elapsed_ms, int center_x, int center_y,
          int body_w, int body_h);
bool validate(uint8_t index);
// No bitmap/animation resource data is linked by this module.
uint32_t resourceBytes();
}
