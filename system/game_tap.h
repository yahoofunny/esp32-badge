#pragma once

#include "game.h"

// Touch-driven counterpart to clawd-roll: clawd pops up somewhere in the arena,
// waits a shrinking moment, and vanishes. Tap it to score.
//
// It exists as much to exercise the game interface as to be played -- it uses touch
// exclusively and drives the framebuffer through the shared gfx layer, where
// clawd-roll leans on tilt and a rotating sprite. Between them they cover both
// input paths and both ends of the drawing cost.
class GameTap : public Game {
public:
    const char *name() const override { return "CLAWD TAP"; }
    uint16_t    accent() const override { return PX(RGB565(0x2E, 0xC4, 0x6B)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;

private:
    void spawn();

    uint16_t *backdrop = nullptr;

    bool  over = false;
    int   score = 0;
    int   misses = 0;
    int   best = 0;

    float tx = 0, ty = 0;           // current clawd position
    float life = 0, lifeMax = 0;    // seconds remaining / total
    float pop = 0;                  // 0..1 appear animation
    float hitFlash = 0;             // 0..1 tap feedback
    int   prevY0 = 0, prevY1 = 0;
};
