#include "game_music.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <soc/soc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "Audio.h"
#include "gfx.h"
#include "input.h"
#include "media.h"

static const char *const MUSIC_EXTS[] = { "mp3", "wav", "m4a", "aac", "flac", "ogg" };
static const int MUSIC_EXT_COUNT = 6;

// The board's I2S pins, from docs/hardware.md.
static const int PIN_MCK = 2, PIN_BCK = 48, PIN_LRCK = 38, PIN_DOUT = 47;

static const int HEAD_Y = 40;
static const int TITLE_Y = 120;
static const int TIME_Y = 168;
static const int BAR_Y = 196;
static const int BTN_Y = 268;
static const int BTN_R = 30;
static const int HINT_Y = 330;

static const uint16_t COL_BG     = PX(RGB565(0x0E, 0x0E, 0x16));
static const uint16_t COL_TEXT   = PX(RGB565(0xEC, 0xEC, 0xF4));
static const uint16_t COL_DIM    = PX(RGB565(0x6E, 0x6E, 0x80));
static const uint16_t COL_ACCENT = PX(RGB565(0xF0, 0x9A, 0x3C));
static const uint16_t COL_WARN   = PX(RGB565(0xE0, 0x50, 0x50));

// Heap, not a global: a static constructor runs before setup(), and exactly that was
// enough to stop the LCD's first transfer when the image decoders were linked that way.
static Audio *player = nullptr;

static const char *NVS_NAMESPACE = "badge";
static const char *NVS_FLAG = "player";

namespace {

bool inCircle(int x, int y, int cx, int cy, int r)
{
    const int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

void drawGlyph(int cx, int cy, int kind)
{
    // 0 = play, 1 = pause, 2 = next, 3 = previous
    const uint16_t c = COL_ACCENT;
    if (kind == 0) {
        for (int i = 0; i < 18; i++) {
            gfx::fillRect(cx - 7 + i, cy - i / 2, 1, i + 1, c);
        }
    } else if (kind == 1) {
        gfx::fillRect(cx - 8, cy - 10, 6, 20, c);
        gfx::fillRect(cx + 2, cy - 10, 6, 20, c);
    } else if (kind == 2) {
        for (int i = 0; i < 18; i++) {
            gfx::fillRect(cx - 9 + i, cy - i / 2, 1, i + 1, c);
        }
        gfx::fillRect(cx + 9, cy - 10, 3, 20, c);
    } else {
        for (int i = 0; i < 18; i++) {
            gfx::fillRect(cx + 8 - i, cy - i / 2, 1, i + 1, c);
        }
        gfx::fillRect(cx - 12, cy - 10, 3, 20, c);
    }
}

}  // namespace

bool GameMusic::bootRequested()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) return false;
    const bool on = prefs.getBool(NVS_FLAG, false);
    prefs.end();
    return on;
}

void GameMusic::requestBoot()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.putBool(NVS_FLAG, true);
    prefs.end();
}

void GameMusic::clearRequest()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    prefs.putBool(NVS_FLAG, false);
    prefs.end();
}

void GameMusic::enter()
{
    // From the carousel this screen does not play anything: it asks for the reboot that
    // lets the player have the audio hardware to itself.
    if (!bootRequested()) {
        Serial.println("[music] requesting the player mode (this reboots)");
        requestBoot();
        delay(120);
        ESP.restart();
        return;
    }

    // The boot we asked for. Clear the flag first: if anything goes wrong from here, the
    // next boot is an ordinary one rather than the same mistake again.
    clearRequest();

    media::begin();
    media::list(media::MUSIC_DIR, MUSIC_EXTS, MUSIC_EXT_COUNT);

    player = new Audio(false, 3, I2S_NUM_0);
    if (!player->setPinout(PIN_BCK, PIN_LRCK, PIN_DOUT, PIN_MCK)) {
        Serial.println("[music] i2s pins refused");
    }
    Serial.printf("[music] player mode, %d file(s) in %s\n", media::count(), media::MUSIC_DIR);
    if (media::count() > 0) {
        player->connecttoFS(SD_MMC, media::pathAt(0));
    }
    setDirty(0, SCR_H - 1);
}

void GameMusic::leave()
{
    if (player) {
        player->stopSong();
    }
    // Nothing of ours can have the I2S while the player's driver is installed, so the way
    // back to the rest of the system is the same reboot that got here.
    Serial.println("[music] leaving the player mode (this reboots)");
    Serial.flush();
    delay(120);
    ESP.restart();
}

void GameMusic::update(float dt)
{
    (void)dt;
    if (player) player->loop();

    if (!input::tapped()) return;

    if (inCircle(input::tapX(), input::tapY(), 120, BTN_Y, BTN_R)) {
        if (media::count() > 0) {          // previous
            int i = 0;
            index_ = (index_ - 1 + media::count()) % media::count();
            player->connecttoFS(SD_MMC, media::pathAt(index_));
        }
        return;
    }
    if (inCircle(input::tapX(), input::tapY(), 180, BTN_Y, BTN_R)) {
        if (player->isRunning()) player->pauseResume();
        else if (media::count() > 0) player->connecttoFS(SD_MMC, media::pathAt(index_));
        return;
    }
    if (inCircle(input::tapX(), input::tapY(), 240, BTN_Y, BTN_R)) {
        if (media::count() > 0) {          // next
            index_ = (index_ + 1) % media::count();
            player->connecttoFS(SD_MMC, media::pathAt(index_));
        }
        return;
    }
    // Anywhere else, near the top: leave. A tap that does nothing is worse than a gesture
    // that is slightly hidden.
    if (input::tapY() < 90) {
        leave();
    }
}

void GameMusic::render()
{
    gfx::clear(COL_BG);
    gfx::textCentered("MUSIC", HEAD_Y, COL_ACCENT, 1);

    if (media::count() <= 0) {
        gfx::textCentered("no music", 150, COL_DIM, 2);
        gfx::textCentered("put files in /Music", 194, COL_TEXT, 1);
        gfx::textCentered("on the card, then come back", 212, COL_DIM, 1);
        gfx::textCentered("tap the top to leave", HINT_Y, COL_DIM, 1);
        return;
    }

    gfx::textCentered(media::nameAt(index_), TITLE_Y, COL_TEXT, 1);

    const uint32_t now = player ? player->getAudioCurrentTime() : 0;
    const uint32_t total = player ? player->getAudioFileDuration() : 0;
    char line[24];
    snprintf(line, sizeof(line), "%u:%02u / %u:%02u", (unsigned)(now / 60),
             (unsigned)(now % 60), (unsigned)(total / 60), (unsigned)(total % 60));
    gfx::textCentered(line, TIME_Y, COL_DIM, 1);

    gfx::fillRect(70, BAR_Y, 220, 6, PX(RGB565(0x28, 0x28, 0x38)));
    if (total > 0 && now <= total) {
        gfx::fillRect(70, BAR_Y, (int)(220.0f * now / total), 6, COL_ACCENT);
    }

    const int bx[3] = { 120, 180, 240 };
    for (int i = 0; i < 3; i++) {
        gfx::strokeCircle((float)bx[i], (float)BTN_Y, (float)BTN_R, 2.0f,
                          gfx::dim(COL_ACCENT, 0.7f));
    }
    drawGlyph(bx[0], BTN_Y, 3);
    drawGlyph(bx[1], BTN_Y, (player && player->isRunning()) ? 1 : 0);
    drawGlyph(bx[2], BTN_Y, 2);

    char head[32];
    snprintf(head, sizeof(head), "%.18s", media::nameAt(index_));
    (void)head;
    gfx::textCentered("tap the top to leave", HINT_Y, COL_DIM, 1);
}

void GameMusic::drawIcon(float cx, float cy, float size) const
{
    // A quaver: a filled head, a stem and a flag.
    const int r = (int)fmaxf(3.0f, size * 0.13f);
    const int hx = (int)cx - (int)(size * 0.16f);
    const int hy = (int)cy + (int)(size * 0.20f);
    gfx::fillCircle((float)hx, (float)hy, (float)r, COL_ACCENT);
    gfx::fillRect(hx + r - 1, hy - (int)(size * 0.46f), (int)(size * 0.07f),
                  (int)(size * 0.46f), COL_ACCENT);
    for (int i = 0; i < (int)(size * 0.26f); i++) {
        gfx::fillRect(hx + r + i, hy - (int)(size * 0.46f) + i, (int)(size * 0.07f), 2,
                      COL_ACCENT);
    }
}
