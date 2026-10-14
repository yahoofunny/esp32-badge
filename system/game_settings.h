#pragma once

#include "game.h"

// Settings: the volume, and what is under the hood.
//
// The volume is the assistant's, kept in the NVS key its own AudioCodec::Start() reads
// at boot ("audio"/"output_volume"). See audio.h for why that key rather than the
// codec's register: the register is rewritten by the assistant on every boot, so
// writing it reaches nothing.
//
// The other two pages are read-only. SYSTEM is the chip, memory and build; NET is what
// the assistant in the other slot is configured with -- its wifi, its server, the
// device id -- because that is the only network this board has, and this firmware never
// joins one. The page says so, rather than letting it read as a live link.
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
    enum Page { PAGE_MENU, PAGE_VOLUME, PAGE_SYSTEM, PAGE_NET };

    Page page = PAGE_MENU;

    // --- the volume page ---
    int  shown = -1;        // the assistant's level, from NVS
    int  raw = -1;          // the codec's register, for the readout
    bool saved = true;      // did the last write read back correctly
    bool dragging = false;

    void refresh();
    void apply(int percent);
    void enterPage(Page p);

    // Where a menu item sits, shared by the drawing and the hit test so the two cannot
    // disagree.
    void menuSlot(int i, float *sx, float *sy) const;

    void updateMenu();
    void updateVolume();
    void updateInfo();      // both read-only pages: only BACK accepts a tap

    void renderMenu();
    void renderVolume();
    void renderSystem();
    void renderNet();

    // The way back, at the top centre. Not the top-left: at that height the round bezel
    // leaves nothing visible out there.
    void renderBack();
};
