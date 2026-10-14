#pragma once

#include "game.h"

// The music in /Music.
//
// This one is a mode rather than a screen, and the reason is the audio hardware. There is
// one I2S peripheral and one set of pins, and the Arduino I2S class this system's sound
// effects use has no way to give them back -- no end(), no deinit. Two things driving the
// same clocks is not a compromise, it is a fault. So the player is entered by rebooting
// into it: the flag below is read in setup(), and when it is set the sound effects never
// start their I2S at all.
//
// The flag means "boot into the player once" and is cleared as the player starts, so a
// crash while playing costs one ordinary boot rather than a loop.
class GameMusic : public Game {
public:
    const char *name() const override { return "MUSIC"; }
    uint16_t    accent() const override { return PX(RGB565(0xF0, 0x9A, 0x3C)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;

    int index_ = 0;

    // Read in setup(): is this boot the one that should come up in the player?
    static bool bootRequested();
    static void requestBoot();
    static void clearRequest();
};
