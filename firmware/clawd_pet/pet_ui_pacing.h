#pragma once
#include <stdint.h>

namespace clawd {
// Input polling always runs at kInputPollMs. Sleep and speech only throttle the
// framebuffer work, so a brief touch can still wake and update the UI immediately.
class UiPacing {
public:
    static constexpr uint32_t kInputPollMs = 20;

    bool renderDue(uint32_t now, bool down, bool event, bool voice, bool sleeping) {
        if (down || event) {
            last_touch_ms_ = now;
            touched_ = true;
        }
        if (!started_ || event) {
            started_ = true;
            last_render_ms_ = now;
            return true;
        }
        const bool interactive = down || (touched_ && uint32_t(now - last_touch_ms_) < 300);
        const uint32_t period = interactive ? 40 : voice ? 100 : sleeping ? 500 : 80;
        const uint32_t elapsed = now - last_render_ms_;
        if (elapsed < period) return false;
        // Advance the original cadence, dropping missed frames rather than
        // accumulating scheduling delays or requesting a burst of catch-up work.
        last_render_ms_ += (elapsed / period) * period;
        return true;
    }

private:
    uint32_t last_render_ms_ = 0;
    uint32_t last_touch_ms_ = 0;
    bool started_ = false;
    bool touched_ = false;
};
} // namespace clawd
