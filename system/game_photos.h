#pragma once

#include "game.h"
#include "photo_gesture.h"

// Built-in Clawd animations followed by /Photos. Swipe/tap left or right to
// browse; holding still toggles the full-screen and complete original views.
class GamePhotos : public Game {
public:
    const char *name() const override { return "PHOTOS"; }
    uint16_t    accent() const override { return PX(RGB565(0x7A, 0x8B, 0xF0)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;
    void next();
    void previous();
    void printDiagnostics() const;
    void captureFrame() const;

private:
    int   index = 0;
    bool  failed = false;
    uint32_t frameDueMs = 0;
    int   frameMs = 0;
    bool needsPresent = true;
    photo::Gesture gesture;

    void showCurrent();
    int pictureCount() const;
    const char *currentName() const;
};
