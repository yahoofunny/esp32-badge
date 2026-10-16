#pragma once
#include <cmath>
#include <cstdint>
#include "gfx.h"

// Shared by JPEG MCU blocks and cooked GIF scanlines. Source coordinates are
// absolute within the image; rx is only subtracted when indexing the local row.
namespace photo {
struct Fit { float scale; int offX, offY; int sourceW, sourceH, width, height; };
inline Fit fitFor(int w, int h, bool fill = false) {
    if (w <= 0 || h <= 0) return {};
    const float sx = float(SCR_W) / w, sy = float(SCR_H) / h;
    const float s = fill ? fmaxf(sx, sy) : fminf(sx, sy);
    const int dw = int(lroundf(w * s)), dh = int(lroundf(h * s));
    return {s, (SCR_W - dw) / 2, (SCR_H - dh) / 2, w, h, dw, dh};
}
inline void blitLine(uint16_t *frame, const uint16_t *px, int rx, int ry, int rw,
                     const Fit &fit, uint32_t *sum = nullptr) {
    if (!frame || !px || fit.scale <= 0 || rw <= 0) return;
    // Integer ratios prevent floating-point rounding holes at MCU boundaries.
    const int y0 = fit.offY + int((int64_t(ry) * fit.height + fit.sourceH - 1) / fit.sourceH);
    const int y1 = fit.offY + int((int64_t(ry + 1) * fit.height + fit.sourceH - 1) / fit.sourceH);
    const int x0 = fit.offX + int((int64_t(rx) * fit.width + fit.sourceW - 1) / fit.sourceW);
    const int x1 = fit.offX + int((int64_t(rx + rw) * fit.width + fit.sourceW - 1) / fit.sourceW);
    for (int y = y0; y < y1; ++y) {
        if ((unsigned)y >= SCR_H) continue;
        for (int x = x0; x < x1; ++x) {
            if ((unsigned)x >= SCR_W) continue;
            const int sx = int(int64_t(x - fit.offX) * fit.sourceW / fit.width);
            if (sx < rx || sx >= rx + rw) continue;
            const uint16_t c = px[sx - rx];
            frame[y * SCR_W + x] = PX(c);
            if (sum) *sum += c;
        }
    }
}
} // namespace photo
