#pragma once

#include "game.h"

// Hands the board over to the XiaoZhi assistant, which ships as its own firmware
// assistant lives.
//
// Not a game at all: it is a screen that performs the switch and then restarts. It
// implements the same interface as the games so the launcher does not need to know
// the difference.
namespace xiaozhi {

// Writes an otadata entry selecting ota_0 and restarts. Returns false if the slot or
// the image is unusable, in which case the caller should stay put.
bool switchToAssistant();

}  // namespace xiaozhi

class GameXiaoZhi : public Game {
public:
    const char *name() const override { return "XIAOZHI"; }
    uint16_t    accent() const override { return PX(RGB565(0x4A, 0x9E, 0xFF)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void update(float dt) override;
    void render() override;

private:
    float t = 0.0f;
    bool  failed = false;
};
