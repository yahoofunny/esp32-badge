#pragma once

#include <stdint.h>

// Shared drawing layer. Everything the system and its games paint goes through
// here, into one full-screen framebuffer owned by the sketch.
//
// The ST77916 expects big-endian RGB565 and the board driver's push functions
// deliberately do not byte-swap, so every colour constant and every pixel written
// is stored pre-swapped. Going through PX() keeps that from being forgotten.

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define BSWAP16(c)      ((uint16_t)((((uint16_t)(c) & 0x00FF) << 8) | (((uint16_t)(c) & 0xFF00) >> 8)))
#define PX(c)           BSWAP16(c)

constexpr int   SCR_W  = 360;
constexpr int   SCR_H  = 360;
constexpr float SCR_CX = 180.0f;
constexpr float SCR_CY = 180.0f;
constexpr float PI_F   = 3.14159265f;

extern uint16_t *fb;                 // full-screen framebuffer, panel byte order

namespace gfx {

inline void pixel(int x, int y, uint16_t c)
{
    if ((unsigned)x < (unsigned)SCR_W && (unsigned)y < (unsigned)SCR_H) {
        fb[y * SCR_W + x] = c;
    }
}

void clear(uint16_t color);
void fillRect(int x, int y, int w, int h, uint16_t color);
void fillCircle(float cx, float cy, float r, uint16_t color);
void strokeCircle(float cx, float cy, float r, float thickness, uint16_t color);
// Ring that fades from `inner` at the inside edge to `outer` at the outside edge.
void gradientRing(float cx, float cy, float r, float thickness, uint16_t inner, uint16_t outer);
void line(float x0, float y0, float x1, float y1, uint16_t color);

// Dims the supplied colour towards black by `amount` (0..1). Used to build
// unselected/selected variants without a second set of constants.
uint16_t dim(uint16_t color, float amount);
// Blends two panel-order colours; t = 0 -> a, t = 1 -> b.
uint16_t mix(uint16_t a, uint16_t b, float t);

// --- text -------------------------------------------------------------------
// 8x8 cell bitmap font, ASCII 32..126. Advance is 7 px per character per scale
// step, which leaves a one-pixel gap between glyphs.
void text(const char *s, int x, int y, uint16_t color, int scale = 1);
int  textWidth(const char *s, int scale = 1);
void textCentered(const char *s, int cy, uint16_t color, int scale = 1);

// How far from the centre a pixel at row y is still on the visible panel. The
// display is a 360 px square behind a round bezel, so the usable width shrinks
// towards the top and bottom: a straight line of text near the edge gets clipped
// even though it is well inside the framebuffer.
float visibleHalfWidth(int y);

// Text laid along a circular arc, centred on the bottom of the circle. This is the
// only way to put a long line near the bottom edge of a round panel without losing
// the ends of it, and it also looks more deliberate than a chord of text.
void textArc(const char *s, float cx, float cy, float radius, uint16_t color, int scale = 1);

// --- sprites ----------------------------------------------------------------
// 2bpp indexed bitmap: index 0 is transparent, 1 and 2 select palette entries.
struct Sprite {
    const uint8_t *bits;
    int            w;
    int            h;
    int            rowBytes;
};

inline uint8_t spriteIndex(const Sprite &s, int x, int y)
{
    return (s.bits[y * s.rowBytes + (x >> 2)] >> ((x & 3) * 2)) & 0x03;
}

// Draws `s` centred on (cx, cy), rotated by `angle` radians and scaled so that its
// circumradius (half its diagonal) equals `radius`. Sampling is nearest-neighbour
// with an alpha test, which is what gives the crisp pixel-art edge.
void drawSpriteRotated(const Sprite &s, float cx, float cy, float angle,
                       float radius, const uint16_t palette[3]);

// Same, but sized by a target width in pixels rather than a circumradius. Handy
// for launcher icons, which are laid out by their footprint.
void drawSpriteSized(const Sprite &s, float cx, float cy, float angle,
                     float targetWidth, const uint16_t palette[3]);

}  // namespace gfx
