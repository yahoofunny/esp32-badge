#pragma once

#include "game.h"

// Settings, which on this badge means the volume.
//
// The stock system's settings screen could control the assistant's volume, and this
// is the same thing: the level belongs to the ES8311 codec, not to a firmware, so
// whatever is set here the assistant inherits. Only the assistant makes noise, so a
// master control is all that is needed -- there is nothing to balance it against.
//
// The screen shows the codec's raw register as well as the percentage. There is no
// other way to see it, and it is the evidence for whether the level is really being
// shared: open this again after the assistant has spoken and the number will either
// be the one set here or the one the assistant wanted.
class GameSettings : public Game {
public:
    const char *name() const override { return "SETTINGS"; }
    uint16_t    accent() const override { return PX(RGB565(0xE0, 0xA8, 0x50)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;

private:
    int  shown = -1;        // the level as last read from the codec
    int  raw = -1;          // its raw register, for the readout
    bool present = false;   // did the codec answer at all
    bool dragging = false;

    void readCodec();
    void apply(int percent);
};
