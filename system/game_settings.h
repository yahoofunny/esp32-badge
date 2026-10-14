#pragma once

#include "game.h"

// Settings, which on this badge means the volume.
//
// The level shown and set here is the assistant's, kept in the NVS key its own
// AudioCodec::Start() reads at boot ("audio"/"output_volume"). See audio.h for why
// that key and not the codec's register: the register is rewritten by the assistant
// on every boot, so writing it reaches nothing.
//
// The codec's register is shown underneath as well. It is not the control -- it is the
// evidence, since it is what the assistant's own conversion of the level produced, and
// there is no other way to see whether the level took.
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
    int  shown = -1;        // the assistant's level, from NVS
    int  raw = -1;          // the codec's register, for the readout
    bool saved = true;      // did the last write read back correctly
    bool dragging = false;

    void refresh();
    void apply(int percent);
};
