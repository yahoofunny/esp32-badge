#pragma once

#include "game.h"

// The home screen: games arranged on a carousel ring, with the selected entry
// parked at the top.
//
// Drag along the orbit to rotate the apps, then release to snap to the nearest.
// A drag never launches an app; a tap on an icon or the centre does. Tilt remains
// available as an alternative and is suppressed while a finger is on the screen.
namespace launcher {

void begin(Game *const *games, int count);

// Returns the game the user launched this frame, or nullptr.
Game *update(float dt);

void render();

}  // namespace launcher
