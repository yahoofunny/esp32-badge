#pragma once

#include "game.h"

// A tutorial that lives on the badge, because the person this is handed to will not go
// looking for a README on a website.
//
// Chinese, which the system's 8x8 ASCII font cannot draw at all -- so this app owns a
// small 16x16 CJK face (tools/make_cjk_font.py) holding exactly the characters these
// pages use. Latin words inside a line are drawn with the system font at four times its
// size, which puts them on the same 32-pixel line.
//
// The pages are short on purpose: one idea each, in a few characters, at a size that is
// readable held at arm's length.
class GameHelp : public Game {
public:
    const char *name() const override { return "HOW TO"; }
    uint16_t    accent() const override { return PX(RGB565(0x5F, 0xC4, 0x7A)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;

private:
    int page = 0;
};
