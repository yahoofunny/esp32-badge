#pragma once

// The clawd sprite in the form the renderer wants it: raw 2bpp indexed bits plus
// the panel-order palette. Kept here rather than in each game so the two games that
// draw clawd cannot drift apart.
//
// `static` gives each translation unit its own copy; the data is a few bytes of
// pointers and four colours, so the duplication is not worth a .cpp.

#include "assets_clawd.h"
#include "gfx.h"

static const gfx::Sprite CLAWD_SPRITE = { clawd_bits, CLAWD_W, CLAWD_H, CLAWD_ROW_BYTES };

// Named CLAWD_SPRITE_PALETTE, not CLAWD_SPRITE_PALETTE: the generated assets_clawd.h already
// defines a macro of the latter name, and a macro would rewrite this declaration.
static const uint16_t CLAWD_SPRITE_PALETTE[3] = {
    0,                              // index 0 is transparent and never read
    PX(CLAWD_BODY_RGB565),
    PX(CLAWD_DETAIL_RGB565),
};
