#pragma once

#include <stdint.h>

// Receiving a file over the serial port.
//
// The card is the badge's content pipe, and pulling it out to put a picture on it
// interrupts the badge. The serial port is already the host's channel to this thing --
// the clock is set over it -- so the same line carries files: the host announces
// "W<bytes> <path>", the badge answers "[file] ready", the bytes follow, and the badge
// answers again with the size and a CRC32 of what it wrote.
//
// It is driven from the main loop a chunk at a time rather than blocking, because a
// transfer must not stop the screens from updating. Only the two content folders are
// writable: the folders are the interface, and the rest of the card is not the badge's
// business.
namespace upload {

// Parses "W<len> <path>". Returns false and says why if it cannot start.
bool start(const char *line);

// Called from the loop while a transfer is in flight.
void poll();

bool active();

}  // namespace upload
