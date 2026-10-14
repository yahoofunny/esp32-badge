#include "game_usbdrive.h"

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <USB.h>
#include <USBMSC.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "input.h"

// The card's own pins. Nothing else on the board uses them, so it can be served while
// the screen keeps running.
static const int SD_CLK = 15, SD_CMD = 14, SD_D0 = 16, SD_D1 = 17, SD_D2 = 12, SD_D3 = 13;

static const int HEAD_Y = 40;
static const int BODY_Y0 = 100;
static const int BODY_PITCH = 16;
static const int HINT_Y = 310;

static const uint16_t COL_BG     = PX(RGB565(0x0E, 0x0E, 0x16));
static const uint16_t COL_TEXT   = PX(RGB565(0xEC, 0xEC, 0xF4));
static const uint16_t COL_DIM    = PX(RGB565(0x6E, 0x6E, 0x80));
static const uint16_t COL_ACCENT = PX(RGB565(0x3F, 0xB6, 0xC0));
static const uint16_t COL_WARN   = PX(RGB565(0xE0, 0x50, 0x50));

static USBMSC msc;
static volatile bool hostGone = false;

// One sector at a time, because the card wrapper only does whole sectors. A partial
// write reads the rest of the sector back first: writing 512 bytes over a 100-byte
// request would wipe the other 412.
static int32_t mscRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    uint8_t *out = (uint8_t *)buffer;
    uint8_t sector[512];
    uint32_t done = 0;
    while (done < bufsize) {
        if (!SD_MMC.readRAW(sector, lba)) return -1;
        uint32_t n = 512 - offset;
        if (n > bufsize - done) n = bufsize - done;
        memcpy(out + done, sector + offset, n);
        done += n;
        lba++;
        offset = 0;
    }
    return (int32_t)bufsize;
}

static int32_t mscWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    uint8_t sector[512];
    uint32_t done = 0;
    while (done < bufsize) {
        if (offset != 0 || bufsize - done < 512) {
            if (!SD_MMC.readRAW(sector, lba)) return -1;
        }
        uint32_t n = 512 - offset;
        if (n > bufsize - done) n = bufsize - done;
        memcpy(sector + offset, buffer + done, n);
        if (!SD_MMC.writeRAW(sector, lba)) return -1;
        done += n;
        lba++;
        offset = 0;
    }
    return (int32_t)bufsize;
}

static bool mscStartStop(uint8_t, bool start, bool)
{
    // The host saying it is finished with the volume. That is the signal worth showing:
    // it is the difference between "you may unplug" and "you may lose a file".
    if (!start) hostGone = true;
    return true;
}

void GameUsbDrive::startMsc()
{
    hostGone = false;

    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
    if (!SD_MMC.begin("/sdcard")) {
        state = ST_NO_CARD;
        Serial.println("[usb] no card");
        return;
    }
    sectors = SD_MMC.numSectors();
    sectorSize = SD_MMC.sectorSize();
    Serial.printf("[usb] card: %d sectors of %d, %llu MB\n", sectors, sectorSize,
                  SD_MMC.cardSize() / (1024ULL * 1024ULL));

    msc.vendorID("esp32");
    msc.productID("clawd-badge-sd");
    msc.productRevision("1.0");
    msc.onStartStop(mscStartStop);
    msc.onRead(mscRead);
    msc.onWrite(mscWrite);
    msc.mediaPresent(true);
    msc.isWritable(true);
    if (!msc.begin(sectors, (uint16_t)sectorSize)) {
        state = ST_NO_CARD;
        Serial.println("[usb] mass storage did not start");
        return;
    }
    USB.begin();
    started = true;
    state = ST_MOUNTED;
    Serial.println("[usb] the card is a drive on the host; there is no COM port until "
                   "this stops");
}

void GameUsbDrive::stopAndReboot()
{
    if (started) {
        msc.end();
        SD_MMC.end();
        Serial.println("[usb] stopped; rebooting so the hardware serial port comes back");
        Serial.flush();
        delay(200);
        ESP.restart();          // does not return
    }
    SD_MMC.end();
    state = ST_STOPPED;
}

void GameUsbDrive::enter()
{
    state = ST_STARTING;
    startMsc();
    setDirty(0, SCR_H - 1);
}

void GameUsbDrive::leave()
{
    stopAndReboot();
    gfx::clear(COL_BG);
}

void GameUsbDrive::update(float dt)
{
    (void)dt;
    if (hostGone) hostEjected = true;

    if (input::tapped()) {
        if (state == ST_MOUNTED) {
            stopAndReboot();
        } else if (state == ST_STOPPED || state == ST_NO_CARD) {
            hostEjected = false;
            state = ST_STARTING;
            startMsc();
        }
    }
    setDirty(0, SCR_H - 1);
}

void GameUsbDrive::render()
{
    gfx::clear(COL_BG);
    gfx::textCentered("USB DRIVE", HEAD_Y, COL_ACCENT, 1);

    char lines[8][32];
    int n = 0;
    auto add = [&](const char *fmt, ...) {
        if (n >= 8) return;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(lines[n], sizeof(lines[n]), fmt, ap);
        va_end(ap);
        n++;
    };

    switch (state) {
    case ST_MOUNTED:
        add("the card is a drive on your");
        add("computer now -- %d.%d GB",
            (int)((double)sectors * sectorSize / 1e9),
            (int)((double)sectors * sectorSize / 1e8) % 10);
        add("");
        if (hostEjected) add("the computer has let go");
        else             add("eject it there first");
        break;
    case ST_STOPPED:
        add("stopped");
        add("");
        add("tap to connect again");
        break;
    case ST_NO_CARD:
        add("no card, or it would not mount");
        add("");
        add("tap to try again");
        break;
    default:
        add("starting...");
        break;
    }

    const int y0 = BODY_Y0 - (n - 1) * BODY_PITCH / 2;
    for (int i = 0; i < n; i++) {
        if (!lines[i][0]) continue;
        const bool warn = (state == ST_NO_CARD) || (state == ST_MOUNTED && !hostEjected);
        gfx::textCentered(lines[i], y0 + i * BODY_PITCH, warn ? COL_WARN : COL_TEXT, 1);
    }

    if (state == ST_MOUNTED) {
        gfx::textCentered(hostEjected ? "tap: stop and reboot"
                                      : "eject there, then tap", HINT_Y, COL_DIM, 1);
    }
}

void GameUsbDrive::drawIcon(float cx, float cy, float size) const
{
    // An SD card: a rectangle with one corner cut, which is the shape everyone already
    // knows and the only thing on this board it could mean.
    const int w = (int)(size * 0.62f);
    const int h = (int)(size * 0.82f);
    const int x = (int)cx - w / 2;
    const int y = (int)cy - h / 2;
    const int cut = (int)(size * 0.16f);
    gfx::fillRect(x + cut, y, w - cut, h, COL_ACCENT);
    gfx::fillRect(x, y + cut, cut, h - cut, COL_ACCENT);
    for (int i = 0; i < cut; i++) {
        gfx::fillRect(x + i, y + cut - 1 - i, 1, cut - i, COL_BG);
    }
    for (int i = 0; i < 3; i++) {
        gfx::fillRect(x + cut + 2 + i * (w / 5), y + 2, 2, (int)(h * 0.18f), COL_BG);
    }
}
