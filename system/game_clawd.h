#pragma once

#include "game.h"

// Tilt-controlled rolling clawd. Tilting the board accelerates it; it rolls, spins
// in proportion to its speed, and bounces off the circular arena wall. No score --
// it is a thing that feels good to wobble.
class GameClawd : public Game {
public:
    const char *name() const override { return "CLAWD ROLL"; }
    uint16_t    accent() const override { return PX(RGB565(0xE8, 0x3B, 0x3B)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;

private:
    // The arena is static, so it is rasterised once into its own buffer and copied
    // back a band at a time. Redrawing it per frame would mean a distance test per
    // pixel across the whole screen.
    uint16_t *backdrop = nullptr;

    float bx = 0, by = 0, vx = 0, vy = 0, roll = 0;
    int   prevY0 = 0, prevY1 = 0;
    float impactX = 0, impactY = 0, impactAge = 0;
};
