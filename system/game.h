#pragma once

#include <stdint.h>

#include "gfx.h"    // game headers use PX()/SCR_H in their inline accessors

// A pluggable game.
//
// The system owns one framebuffer and one input layer; a game only simulates and
// paints. Nothing here knows about the launcher, which is what keeps adding a game
// to "write a class, add one line to the table in system.ino".
class Game {
public:
    virtual ~Game() {}

    // Shown under the launcher icon.
    virtual const char *name() const = 0;

    // Panel-order colour used for the launcher tile and any accents the launcher
    // draws around the selected entry.
    virtual uint16_t accent() const = 0;

    // Paints the launcher icon centred on (cx, cy), occupying roughly `size`
    // pixels across. The default is a plain disc in the accent colour; games with
    // their own art override it.
    virtual void drawIcon(float cx, float cy, float size) const;

    virtual void enter() {}                 // becoming the active screen
    virtual void leave() {}                 // being replaced
    virtual void update(float dt) = 0;      // advance one frame
    virtual void render() = 0;              // paint into the shared framebuffer

    // Rows of the framebuffer that the last render() changed. The system transfers
    // only that band to the panel, so a game that repaints a small moving object
    // does not pay for a full-screen push every frame.
    //
    // The default is the whole screen, which is always correct. A game that narrows
    // it takes on the obligation of leaving every other row exactly as it was --
    // the panel keeps whatever it was last sent.
    int dirtyY0 = 0;
    int dirtyY1 = SCR_H - 1;

protected:
    void setDirty(int y0, int y1)
    {
        dirtyY0 = (y0 < 0) ? 0 : y0;
        dirtyY1 = (y1 > SCR_H - 1) ? SCR_H - 1 : y1;
    }
};
