// Native regression test: g++ -std=c++17 tests/photo_blit_test.cpp -o build/photo_blit_test.exe
#include "../system/photo_blit.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>

static void check(int w, int h, int block, bool fill) {
    constexpr uint16_t untouched = 0xdead;
    std::vector<uint16_t> frame(SCR_W * SCR_H, untouched);
    std::vector<uint16_t> row(block);
    const auto fit = photo::fitFor(w, h, fill);
    // Independent source pattern: every column has a distinct colour; several MCU
    // blocks must combine into one complete image, including partial right blocks.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; x += block) {
            const int used = std::min(block, w - x);
            for (int i = 0; i < used; ++i) row[i] = uint16_t(1 + x + i);
            photo::blitLine(frame.data(), row.data(), x, y, used, fit);
        }
    }
    const int dw = fit.width, dh = fit.height;
    for (int y = 0; y < SCR_H; ++y) for (int x = 0; x < SCR_W; ++x) {
        const bool inside = x >= fit.offX && x < fit.offX + dw &&
                            y >= fit.offY && y < fit.offY + dh;
        if (inside) {
            const int sourceX = int(int64_t(x - fit.offX) * w / fit.width);
            if (sourceX >= w) continue;
            const uint16_t expected = PX(uint16_t(sourceX + 1));
            if (frame[y * SCR_W + x] != expected) {
                std::printf("FAIL %dx%d block %d at %d,%d: %x expected %x\n",
                    w, h, block, x, y, frame[y * SCR_W + x], expected);
                std::abort();
            }
        } else assert(frame[y * SCR_W + x] == untouched);
    }
}
int main() {
    for (auto dims : {std::pair{360,360}, {97,61}, {641,479}, {1200,800}, {173,511}})
        for (int block : {8,16,64}) for (bool fill : {false,true})
            check(dims.first, dims.second, block, fill);
    std::puts("PASS: multi-block coverage, odd edges, up/downscale, fit and fill crop");
}
