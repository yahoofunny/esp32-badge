#pragma once

#include "game.h"

// Settings, which on this badge means the volume.
//
// The level is written into the ES8311 codec, which has no reset line on this board,
// so it survives resets and slot switches. That was the hope for reaching the
// assistant too, and it does not work: measured on the device, the assistant writes
// this same register itself on every boot (25% set here came back as 66%), and it
// keeps no copy in NVS -- so its level is compiled into its firmware. This control
// therefore covers anything of ours that makes a sound, and cannot reach the
// assistant. The screen says so rather than implying otherwise.
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
