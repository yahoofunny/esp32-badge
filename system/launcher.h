#pragma once

#include "game.h"

// Navigation tiles share the app carousel but never become active game screens.
class GameFolder : public Game {
public:
    const char *name() const override { return "GAMES"; }
    uint16_t accent() const override { return PX(RGB565(0xE8, 0xB8, 0x55)); }
    void drawIcon(float cx, float cy, float size) const override {
        const int x = int(cx - size * .46f), y = int(cy - size * .24f);
        gfx::fillRect(x, y - int(size * .13f), int(size * .39f), int(size * .2f), accent());
        gfx::fillRect(x, y, int(size * .92f), int(size * .60f), accent());
        const uint16_t ink = PX(RGB565(0x24, 0x25, 0x39));
        gfx::fillRect(int(cx - size * .3f), int(cy - size * .02f), int(size * .6f), int(size * .26f), ink);
        gfx::fillRect(int(cx - size * .24f), int(cy + size * .08f), int(size * .18f), int(size * .05f), accent());
        gfx::fillRect(int(cx - size * .18f), int(cy + size * .02f), int(size * .05f), int(size * .18f), accent());
        gfx::fillCircle(cx + size * .14f, cy + size * .06f, size * .04f, accent());
        gfx::fillCircle(cx + size * .23f, cy + size * .16f, size * .04f, accent());
    }
    void update(float) override {}
    void render() override {}
};

class GameBack : public Game {
public:
    const char *name() const override { return "BACK"; }
    uint16_t accent() const override { return PX(RGB565(0xB6, 0xBA, 0xD4)); }
    void drawIcon(float cx, float cy, float size) const override {
        const int x = int(cx - size * .35f), y = int(cy);
        for (int d = -1; d <= 1; ++d) {
            gfx::line(x, y + d, cx + size * .35f, y + d, accent());
            gfx::line(x, y + d, cx, cy - size * .30f + d, accent());
            gfx::line(x, y + d, cx, cy + size * .30f + d, accent());
        }
    }
    void update(float) override {}
    void render() override {}
};

// The home screen: games arranged on a carousel ring, with the selected entry
// parked at the top.
//
// Drag along the orbit to rotate the apps, then release to snap to the nearest.
// A drag never launches an app; a tap on an icon or the centre does. Tilt remains
// available as an alternative and is suppressed while a finger is on the screen.
namespace launcher {

void begin(Game *const *games, int count, int selected = 0);
int selectedIndex();

// Returns the game the user launched this frame, or nullptr.
Game *update(float dt);

void render();

// Does the picture need repainting this frame? Everything on this screen is static
// except the breathing ring around the selected entry, so once the carousel has settled
// and nothing is being touched the caller can paint and send far less often. Anything
// that actually changes the picture -- a drag, a lean, a launch, the BOOT hold ring --
// asks for every frame.
bool wantsFrame();

}  // namespace launcher
