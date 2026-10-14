#pragma once

#include <stdint.h>

// The shared arena backdrop: dark field, circular wall, floor dots. Both games
// present their play area this way, so it is rasterised in one place.
//
// It is static, so each game rasterises it once on enter() and then copies back a
// band at a time while rendering. Repainting it per frame would mean a distance
// test for every pixel on the screen, every frame.
namespace arena {

// Allocates the backdrop in PSRAM and paints it. Returns nullptr if the allocation
// fails; the caller owns the buffer and frees it on leave().
uint16_t *build(uint16_t ringColor);

// Convenience: copies rows [y0, y1] of the backdrop into the live framebuffer.
void restore(uint16_t *backdrop, int y0, int y1);

}  // namespace arena
