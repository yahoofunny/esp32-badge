#pragma once

#include "game.h"

// The pictures in /Photos, full screen.
//
// Three formats for three reasons: BMP because it is a few lines to read and is the
// lossless one for pixel art, JPEG because that is what a camera produces, and GIF
// because an animated one is half the point of a badge sitting on a desk.
//
// The folder is the interface -- see media.h. Drop files in and they appear, in the order
// the card lists them; a tap moves to the next.
class GamePhotos : public Game {
public:
    const char *name() const override { return "PHOTOS"; }
    uint16_t    accent() const override { return PX(RGB565(0x7A, 0x8B, 0xF0)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;

private:
    int   index = 0;
    bool  failed = false;
    float frameSec = 0.0f;
    int   frameMs = 0;
    bool  longPressFired = false;

    void showCurrent();
};
