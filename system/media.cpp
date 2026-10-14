#include "media.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <string.h>

// The card's pins, which nothing else on the board uses.
static const int SD_CLK = 15, SD_CMD = 14, SD_D0 = 16, SD_D1 = 17, SD_D2 = 12, SD_D3 = 13;

// Enough for a folder of pictures or a folder of songs, and small enough to be a static
// buffer rather than something to manage. Names are longer than 8.3 because the card is
// FAT with long names.
static const int MAX_FILES = 64;
static const int MAX_PATH = 96;

static bool isMounted = false;
static int fileCount = 0;
static char paths[MAX_FILES][MAX_PATH];
static char names[MAX_FILES][48];

static bool hasExtension(const char *name, const char *const *exts, int extCount)
{
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    for (int i = 0; i < extCount; i++) {
        const char *a = dot + 1;
        const char *b = exts[i];
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
            a++;
            b++;
        }
        if (*a == 0 && *b == 0) return true;
    }
    return false;
}

namespace media {

bool begin()
{
    if (isMounted) return true;

    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
    if (!SD_MMC.begin("/sdcard")) {
        Serial.println("[media] no card");
        return false;
    }
    isMounted = true;

    // Created here rather than left to the user: the folders are the interface, and they
    // should exist the first time the card is plugged into a computer.
    if (!SD_MMC.exists(MUSIC_DIR)) SD_MMC.mkdir(MUSIC_DIR);
    if (!SD_MMC.exists(PHOTOS_DIR)) SD_MMC.mkdir(PHOTOS_DIR);
    Serial.printf("[media] card up, %llu MB free\n",
                  (unsigned long long)(SD_MMC.totalBytes() - SD_MMC.usedBytes()) /
                      (1024ULL * 1024ULL));
    return true;
}

void end()
{
    if (!isMounted) return;
    SD_MMC.end();
    isMounted = false;
    fileCount = 0;
}

bool mounted() { return isMounted; }

int list(const char *dir, const char *const *extensions, int extensionCount)
{
    fileCount = 0;
    if (!isMounted) return 0;

    File folder = SD_MMC.open(dir);
    if (!folder || !folder.isDirectory()) {
        Serial.printf("[media] no folder %s\n", dir);
        return 0;
    }

    for (File f = folder.openNextFile(); f && fileCount < MAX_FILES;
         f = folder.openNextFile()) {
        const char *nm = f.name();
        if (f.isDirectory() || !hasExtension(nm, extensions, extensionCount)) continue;
        // The name may or may not include the folder, depending on the core's version;
        // normalise to a full path.
        if (nm[0] == '/') snprintf(paths[fileCount], MAX_PATH, "%s", nm);
        else snprintf(paths[fileCount], MAX_PATH, "%s/%s", dir, nm);
        const char *base = strrchr(paths[fileCount], '/');
        snprintf(names[fileCount], sizeof(names[0]), "%s", base ? base + 1 : nm);
        fileCount++;
    }
    Serial.printf("[media] %s: %d file(s)\n", dir, fileCount);
    return fileCount;
}

int count() { return fileCount; }

const char *pathAt(int i)
{
    return (i >= 0 && i < fileCount) ? paths[i] : "";
}

const char *nameAt(int i)
{
    return (i >= 0 && i < fileCount) ? names[i] : "";
}

}  // namespace media
