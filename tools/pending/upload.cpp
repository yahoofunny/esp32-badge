#include "upload.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <string.h>

#include "media.h"

// The CRC has to be one a host can reproduce, which means zlib's (ISO-HDLC). ESP-IDF's
// esp_rom_crc32_le is *not* it -- it inverts both its input and its output, the same
// quirk the otadata notes in the README run into -- and a verification path is the last
// place to discover that.
static uint32_t crc32Update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static const uint32_t IDLE_TIMEOUT_MS = 10000;      // a stalled host, not a slow one
static const uint32_t MAX_BYTES = 64u * 1024 * 1024;

// A pointer, not an object: a global constructor runs before setup(), and on this board
// exactly that was enough to make the LCD's first SPI transfer fail (see the heap note
// in the README about the image decoders). The file is opened when a transfer starts.
static File *file = nullptr;
static bool inFlight = false;
static uint32_t remaining = 0;
static uint32_t written = 0;
static uint32_t crc = 0;
static uint32_t lastByteMs = 0;
static char path[96];
static uint8_t buf[4096];

bool upload::active() { return inFlight; }

static void fail(const char *why)
{
    Serial.printf("[file] err %s\n", why);
    if (file) {
        file->close();
        delete file;
        file = nullptr;
    }
    SD_MMC.remove(path);        // no half-written picture left behind
    inFlight = false;
}

bool upload::start(const char *line)
{
    const char *p = line + 1;               // past the 'W'
    char *end = nullptr;
    const unsigned long len = strtoul(p, &end, 10);
    while (end && *end == ' ') end++;
    if (end == p || !end || *end != '/') {
        Serial.println("[file] err want: W<bytes> </Music|/Photos/name>");
        return false;
    }
    if (len == 0 || len > MAX_BYTES) {
        Serial.println("[file] err length out of range");
        return false;
    }
    const size_t photoLen = strlen(media::PHOTOS_DIR);
    const size_t musicLen = strlen(media::MUSIC_DIR);
    if ((strncmp(end, media::PHOTOS_DIR, photoLen) != 0 || end[photoLen] != '/') &&
        (strncmp(end, media::MUSIC_DIR, musicLen) != 0 || end[musicLen] != '/')) {
        Serial.println("[file] err only /Photos and /Music are writable");
        return false;
    }
    if (!media::begin()) {
        Serial.println("[file] err no card");
        return false;
    }

    snprintf(path, sizeof(path), "%s", end);
    File opened = SD_MMC.open(path, FILE_WRITE);   // creates, or truncates what was there
    if (!opened) {
        Serial.printf("[file] err cannot open %s\n", path);
        return false;
    }
    file = new File(opened);

    remaining = (uint32_t)len;
    written = 0;
    crc = 0;
    lastByteMs = millis();
    inFlight = true;
    Serial.printf("[file] ready %lu\n", len);
    return true;
}

void upload::poll()
{
    if (!inFlight) return;

    // One chunk per pass, so the frame loop keeps running through a transfer.
    const size_t want = remaining < sizeof(buf) ? (size_t)remaining : sizeof(buf);
    size_t got = 0;
    while (got < want && Serial.available() > 0) {
        buf[got++] = (uint8_t)Serial.read();
    }

    if (got > 0) {
        if (!file || file->write(buf, got) != got) {
            fail("write failed");
            return;
        }
        crc = crc32Update(crc, buf, got);
        written += got;
        remaining -= got;
        lastByteMs = millis();
    } else if (millis() - lastByteMs > IDLE_TIMEOUT_MS) {
        fail("timeout");
        return;
    }

    if (remaining == 0) {
        file->close();
        delete file;
        file = nullptr;
        Serial.printf("[file] wrote %s %lu bytes crc32=0x%08lX\n",
                      path, (unsigned long)written, (unsigned long)crc);
        inFlight = false;
    }
}
