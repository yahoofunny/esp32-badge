#include "gfx.h"

#include <math.h>
#include <string.h>

#include "assets_font.h"

uint16_t *fb = nullptr;

namespace gfx {

void clear(uint16_t color)
{
    const uint32_t v = ((uint32_t)color << 16) | color;
    uint32_t *p = (uint32_t *)fb;
    for (int i = 0; i < (SCR_W * SCR_H) / 2; i++) {
        p[i] = v;
    }
}

void fillRect(int x, int y, int w, int h, uint16_t color)
{
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w; if (x1 > SCR_W) x1 = SCR_W;
    int y1 = y + h; if (y1 > SCR_H) y1 = SCR_H;
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *row = fb + (size_t)yy * SCR_W;
        for (int xx = x0; xx < x1; xx++) row[xx] = color;
    }
}

void fillCircle(float cx, float cy, float r, uint16_t color)
{
    const float r2 = r * r;
    int x0 = (int)floorf(cx - r); if (x0 < 0) x0 = 0;
    int x1 = (int)ceilf (cx + r); if (x1 > SCR_W - 1) x1 = SCR_W - 1;
    int y0 = (int)floorf(cy - r); if (y0 < 0) y0 = 0;
    int y1 = (int)ceilf (cy + r); if (y1 > SCR_H - 1) y1 = SCR_H - 1;

    for (int y = y0; y <= y1; y++) {
        const float dy = (y + 0.5f) - cy;
        const float dy2 = dy * dy;
        if (dy2 > r2) continue;
        const float half = sqrtf(r2 - dy2);
        int sx = (int)(cx - half); if (sx < 0) sx = 0;
        int ex = (int)(cx + half); if (ex > SCR_W - 1) ex = SCR_W - 1;
        uint16_t *row = fb + (size_t)y * SCR_W;
        for (int x = sx; x <= ex; x++) row[x] = color;
    }
}

void strokeCircle(float cx, float cy, float r, float thickness, uint16_t color)
{
    const float half = thickness * 0.5f;
    const float rIn = r - half;
    const float rOut = r + half;
    // Comparing squared distances avoids a square root for every interior pixel.
    // A nonpositive inner radius has no hole (thick strokes can fill the disc).
    const float rIn2 = rIn > 0.0f ? rIn * rIn : 0.0f;
    const float rOut2 = rOut * rOut;

    int x0 = (int)floorf(cx - rOut); if (x0 < 0) x0 = 0;
    int x1 = (int)ceilf (cx + rOut); if (x1 > SCR_W - 1) x1 = SCR_W - 1;
    int y0 = (int)floorf(cy - rOut); if (y0 < 0) y0 = 0;
    int y1 = (int)ceilf (cy + rOut); if (y1 > SCR_H - 1) y1 = SCR_H - 1;

    for (int y = y0; y <= y1; y++) {
        const float dy = (y + 0.5f) - cy;
        uint16_t *row = fb + (size_t)y * SCR_W;
        for (int x = x0; x <= x1; x++) {
            const float dx = (x + 0.5f) - cx;
            const float d2 = dx * dx + dy * dy;
            if (d2 > rOut2) continue;
            if (d2 < rIn2) continue;
            row[x] = color;
        }
    }
}

void gradientRing(float cx, float cy, float r, float thickness, uint16_t inner, uint16_t outer)
{
    const float half = thickness * 0.5f;
    const float rIn = r - half;
    const float rOut = r + half;
    const float rOut2 = rOut * rOut;

    int x0 = (int)floorf(cx - rOut); if (x0 < 0) x0 = 0;
    int x1 = (int)ceilf (cx + rOut); if (x1 > SCR_W - 1) x1 = SCR_W - 1;
    int y0 = (int)floorf(cy - rOut); if (y0 < 0) y0 = 0;
    int y1 = (int)ceilf (cy + rOut); if (y1 > SCR_H - 1) y1 = SCR_H - 1;

    for (int y = y0; y <= y1; y++) {
        const float dy = (y + 0.5f) - cy;
        uint16_t *row = fb + (size_t)y * SCR_W;
        for (int x = x0; x <= x1; x++) {
            const float dx = (x + 0.5f) - cx;
            const float d2 = dx * dx + dy * dy;
            if (d2 > rOut2) continue;
            const float d = sqrtf(d2);
            if (d < rIn) continue;
            row[x] = mix(inner, outer, (d - rIn) / thickness);
        }
    }
}

void line(float x0, float y0, float x1, float y1, uint16_t color)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const int steps = (int)(fmaxf(fabsf(dx), fabsf(dy)) + 1.0f);
    for (int i = 0; i <= steps; i++) {
        const float t = steps ? (float)i / steps : 0.0f;
        pixel((int)(x0 + dx * t), (int)(y0 + dy * t), color);
    }
}

// --- colour helpers ---------------------------------------------------------
// Channel-wise moves in RGB565 space. Cheap and good enough for UI shading; the
// round-trip through host order is fine here because it happens once per colour,
// not once per pixel.
static inline void unpack(uint16_t c, int *r, int *g, int *b)
{
    const uint16_t h = PX(c);          // back to host-order RGB565 on either backend
    *r = (h >> 11) & 0x1F;
    *g = (h >> 5)  & 0x3F;
    *b =  h        & 0x1F;
}

static inline uint16_t pack(int r, int g, int b)
{
    r = r < 0 ? 0 : (r > 31 ? 31 : r);
    g = g < 0 ? 0 : (g > 63 ? 63 : g);
    b = b < 0 ? 0 : (b > 31 ? 31 : b);
    return PX((uint16_t)((r << 11) | (g << 5) | b));
}

uint16_t dim(uint16_t color, float amount)
{
    int r, g, b;
    unpack(color, &r, &g, &b);
    const float k = 1.0f - amount;
    return pack((int)(r * k + 0.5f), (int)(g * k + 0.5f), (int)(b * k + 0.5f));
}

uint16_t mix(uint16_t a, uint16_t b, float t)
{
    int ar, ag, ab, br, bg, bb;
    unpack(a, &ar, &ag, &ab);
    unpack(b, &br, &bg, &bb);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return pack((int)(ar + (br - ar) * t + 0.5f),
                (int)(ag + (bg - ag) * t + 0.5f),
                (int)(ab + (bb - ab) * t + 0.5f));
}

// --- text -------------------------------------------------------------------
// The generated font stores one byte per row with the leftmost pixel in the MSB,
// so a glyph is eight shift tests per character. No clipping maths beyond the
// framebuffer bounds check inside pixel().
static const int GLYPH_ADVANCE = 7;

void text(const char *s, int x, int y, uint16_t color, int scale)
{
    if (scale < 1) scale = 1;
    for (; *s; s++) {
        const unsigned char c = (unsigned char)*s;
        if (c < FONT_FIRST || c > FONT_LAST) { x += GLYPH_ADVANCE * scale; continue; }
        const uint8_t *rows = font8x8[c - FONT_FIRST];
        for (int gy = 0; gy < FONT_CELL; gy++) {
            const uint8_t bits = rows[gy];
            if (!bits) continue;
            for (int gx = 0; gx < FONT_CELL; gx++) {
                if (!(bits & (0x80 >> gx))) continue;
                if (scale == 1) {
                    pixel(x + gx, y + gy, color);
                } else {
                    fillRect(x + gx * scale, y + gy * scale, scale, scale, color);
                }
            }
        }
        x += GLYPH_ADVANCE * scale;
    }
}

int textWidth(const char *s, int scale)
{
    if (scale < 1) scale = 1;
    const int n = (int)strlen(s);
    return n ? n * GLYPH_ADVANCE * scale - (GLYPH_ADVANCE - FONT_CELL) * scale : 0;
}

void textCentered(const char *s, int cy, uint16_t color, int scale)
{
    const int w = textWidth(s, scale);
    text(s, (int)(SCR_CX - w * 0.5f), cy, color, scale);
}

float visibleHalfWidth(int y)
{
    // The panel is a circle of radius SCR_W/2 centred on the screen. Anything a
    // pixel outside that is behind the bezel.
    const float r = SCR_W * 0.5f;
    const float dy = (float)y - SCR_CY;
    const float d2 = r * r - dy * dy;
    return d2 <= 0.0f ? 0.0f : sqrtf(d2);
}

// Arc text was tried here and removed: see the note in gfx.h. It is kept out of
// the tree rather than left unused, because it looks like it works and quietly
// mangles the glyphs.


// --- sprites ----------------------------------------------------------------
void drawSpriteRotated(const Sprite &s, float cx, float cy, float angle,
                       float radius, const uint16_t palette[4])
{
    // Map the sprite's circumradius onto the requested radius, then for each
    // destination pixel run the inverse transform back into source space. Only the
    // bounding box is visited, so cost tracks the sprite's on-screen size.
    const float circum = sqrtf((s.w * 0.5f) * (s.w * 0.5f) + (s.h * 0.5f) * (s.h * 0.5f));
    if (circum <= 0.0f || radius <= 0.0f) return;
    const float inv = circum / radius;

    const float ca = cosf(angle);
    const float sa = sinf(angle);

    int x0 = (int)floorf(cx - radius); if (x0 < 0) x0 = 0;
    int x1 = (int)ceilf (cx + radius); if (x1 > SCR_W - 1) x1 = SCR_W - 1;
    int y0 = (int)floorf(cy - radius); if (y0 < 0) y0 = 0;
    int y1 = (int)ceilf (cy + radius); if (y1 > SCR_H - 1) y1 = SCR_H - 1;

    const float hw = s.w * 0.5f;
    const float hh = s.h * 0.5f;

    for (int py = y0; py <= y1; py++) {
        const float dy = (py + 0.5f) - cy;
        uint16_t *row = fb + (size_t)py * SCR_W;
        for (int px = x0; px <= x1; px++) {
            const float dx = (px + 0.5f) - cx;
            const float rx =  dx * ca + dy * sa;
            const float ry = -dx * sa + dy * ca;
            const int sx = (int)(rx * inv + hw);
            const int sy = (int)(ry * inv + hh);
            if ((unsigned)sx >= (unsigned)s.w || (unsigned)sy >= (unsigned)s.h) continue;
            const uint8_t idx = spriteIndex(s, sx, sy);
            if (idx == 0) continue;
            row[px] = palette[idx];
        }
    }
}

void drawSpriteSized(const Sprite &s, float cx, float cy, float angle,
                     float targetWidth, const uint16_t palette[4])
{
    const float circum = sqrtf((s.w * 0.5f) * (s.w * 0.5f) + (s.h * 0.5f) * (s.h * 0.5f));
    // radius such that the horizontal extent comes out as targetWidth
    drawSpriteRotated(s, cx, cy, angle, circum * (targetWidth / (float)s.w), palette);
}

}  // namespace gfx
