#include "arena.h"

#include <math.h>
#include <string.h>

#include "Display_ST77916.h"
#include "gfx.h"

namespace arena {

static const float ARENA = 172.0f;      // inner edge of the wall

static const uint16_t COL_BG      = PX(RGB565(0x0B, 0x0B, 0x12));
static const uint16_t COL_OUTSIDE = PX(RGB565(0x06, 0x06, 0x0A));
static const uint16_t COL_GLOW    = PX(RGB565(0x24, 0x24, 0x52));
static const uint16_t COL_DOT     = PX(RGB565(0x1A, 0x1A, 0x2E));

uint16_t *build(uint16_t ringColor)
{
    uint16_t *buf = (uint16_t *)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM);
    if (!buf) return nullptr;

    for (int y = 0; y < SCR_H; y++) {
        for (int x = 0; x < SCR_W; x++) {
            const float dx = (x + 0.5f) - SCR_CX;
            const float dy = (y + 0.5f) - SCR_CY;
            const float d = sqrtf(dx * dx + dy * dy);

            uint16_t c;
            if (d > ARENA + 6.0f) {
                c = COL_OUTSIDE;                       // dark band beyond the wall
            } else if (d > ARENA - 1.5f) {
                c = ringColor;                         // the wall itself
            } else if (d > ARENA - 6.0f) {
                c = COL_GLOW;                          // inner glow, sells the edge
            } else if (((x % 45) == 22) && ((y % 45) == 22)) {
                c = COL_DOT;                           // sparse floor dots
            } else {
                c = COL_BG;
            }
            buf[y * SCR_W + x] = c;
        }
    }
    return buf;
}

void restore(uint16_t *backdrop, int y0, int y1)
{
    if (!backdrop) return;
    if (y0 < 0) y0 = 0;
    if (y1 > SCR_H - 1) y1 = SCR_H - 1;
    for (int y = y0; y <= y1; y++) {
        memcpy(fb + (size_t)y * SCR_W, backdrop + (size_t)y * SCR_W, SCR_W * 2);
    }
}

}  // namespace arena
