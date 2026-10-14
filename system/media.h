#pragma once

#include <stdint.h>

// The card, and the two folders that are the whole convention.
//
// The card is 58 GB. Scanning all of it for playable or displayable files every time a
// screen opens would be slow, and would turn up whatever else happens to be there. Two
// fixed folders make the rule the user's: drop files in, they appear. Anything else on
// the card is left alone.
namespace media {

constexpr const char *MUSIC_DIR = "/Music";
constexpr const char *PHOTOS_DIR = "/Photos";

// Mounts the card and creates both folders if they are missing, so the folders exist
// before anyone plugs the card into a computer to fill them.
bool begin();
void end();
bool mounted();

// Lists one folder, filtered by extension, into a fixed-size cache: the names are kept so
// that drawing a screen does not re-read the directory every frame. Returns the count.
int list(const char *dir, const char *const *extensions, int extensionCount);
int count();

// The full path of entry i, or "" if there is none.
const char *pathAt(int i);
// Just the file name, for anything that has to show it.
const char *nameAt(int i);

}  // namespace media
