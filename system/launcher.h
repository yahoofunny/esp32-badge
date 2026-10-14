#pragma once

#include "game.h"

// The home screen: games arranged on a carousel ring, with the selected entry
// parked at the top.
//
// Interaction is tilt-first, because that is what this device has: leaning the
// board past a threshold steps the carousel one place and then latches until the
// board comes back level, so a single lean moves exactly one game rather than
// spinning through them. Touching an icon launches it directly, which makes the
// whole thing usable without touching the tilt at all.
namespace launcher {

void begin(Game *const *games, int count);

// Returns the game the user launched this frame, or nullptr.
Game *update(float dt);

void render();

}  // namespace launcher
