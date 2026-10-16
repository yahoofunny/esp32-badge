/*
 * esp32-badge — a small self-contained system for the Waveshare
 * ESP32-S3-Touch-LCD-1.85B.
 *
 * Boots into a launcher ring of games, each of which is an independent module
 * behind the Game interface. Adding one means writing a class and appending a
 * pointer to GAMES below.
 *
 * This firmware lives in the idle `ota_0` partition; the stock Waveshare system
 * (which includes the XiaoZhi AI app) stays untouched in `factory`. Holding BOOT on
 * the launcher hands the board back to it. See tools/switch_slot.py and the README.
 *
 * Rendering is direct: one full-screen RGB565 framebuffer in PSRAM, written by the
 * gfx layer and pushed to the panel by the board driver. No LVGL, which keeps the
 * frame loop cheap and sidesteps the 8.x/9.x API split between the two Waveshare
 * example trees.
 */

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "Display_ST77916.h"
#include "I2C_Driver.h"
#include "arena.h"
#include "audio.h"
#include "clawd_sprite.h"
#include "esp_heap_caps.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "game.h"
#include "game_clawd.h"
#include "game_mon.h"
#include "game_pet.h"
#include "game_photos.h"
#include "game_settings.h"
#include "game_tap.h"
#include "game_xiaozhi.h"
#include "gfx.h"
#include "input.h"
#include "launcher.h"
#include "clock.h"
#include "power.h"
#include "sound.h"

// ------------------------------------------------------------------- games ---
// The whole registry. Order is the order they appear on the launcher ring.
//
// Entries are "screens", not strictly games: GameXiaoZhi is an action that hands the
// board over to the stock system. The launcher only needs each one to name itself,
// draw an icon, and be enterable.
static GameClawd   g_clawd;
static GameTap     g_tap;
static GamePet     g_pet;
static GameMon     g_mon;
static GameXiaoZhi g_xiaozhi;
static GameSettings g_settings;
static GamePhotos   g_photos;

static Game *const GAMES[] = { &g_clawd, &g_tap, &g_pet, &g_mon, &g_photos,
                               &g_xiaozhi, &g_settings };
static const int   GAME_COUNT = (int)(sizeof(GAMES) / sizeof(GAMES[0]));

// ----------------------------------------------------------------- screens ---
enum Screen { SCREEN_LAUNCHER, SCREEN_GAME };

static Screen screen = SCREEN_LAUNCHER;
static Game  *active = nullptr;

// ------------------------------------------------------------- boot button ---
// One gesture, two meanings by context: inside a game it returns to the launcher,
// on the launcher it hands the board back to the stock system. Keeping the
// durations distinct means the destructive one cannot happen by accident while
// mashing to leave a game.
static const unsigned long HOLD_BACK_TO_LAUNCHER = 900;
static const unsigned long HOLD_BACK_TO_STOCK    = 2200;

static bool          bootFired = false;

// The launcher's long press does the same thing as the XIAOZHI entry: it selects the
// ota_0 slot and restarts. Note it does NOT set the boot target to `factory` -- in
// this layout factory *is* this firmware, so that would be a no-op. See
// partitions.csv for how the two slots hand the board back and forth.
static void handleBootButton()
{
    const unsigned long held = input::bootHeldMs();

    if (!input::bootDown()) {
        bootFired = false;
        return;
    }
    if (bootFired) return;

    const unsigned long threshold =
        (screen == SCREEN_GAME) ? HOLD_BACK_TO_LAUNCHER : HOLD_BACK_TO_STOCK;

    if (held >= threshold) {
        bootFired = true;
        if (screen == SCREEN_GAME) {
            active->leave();
            active = nullptr;
            screen = SCREEN_LAUNCHER;
            Serial.println("[sys] back to the launcher");
        } else {
            Serial.println("[sys] handing over to the stock system");
            xiaozhi::switchToAssistant();
        }
    }
}

// Hold indicator: a ring that fills as the button is held, so the gesture is
// discoverable rather than a secret. Drawn in the bottom rows and folded into the
// pushed band so it does not require a separate transfer.
static const int   HINT_CY = SCR_H - 26;
static const int   HINT_R  = 11;

static void drawHoldIndicator(unsigned long held, unsigned long threshold,
                              int *bandY0, int *bandY1)
{
    if (held < 220 || held >= threshold) return;

    const float t = (float)(held - 220) / (float)(threshold - 220);
    gfx::fillCircle(SCR_CX, HINT_CY, HINT_R + 2, PX(RGB565(0x14, 0x14, 0x20)));
    gfx::strokeCircle(SCR_CX, HINT_CY, HINT_R, 2.5f, PX(RGB565(0x2E, 0x2E, 0x44)));
    gfx::fillCircle(SCR_CX, HINT_CY, HINT_R * t, PX(RGB565(0x8A, 0x8A, 0xC8)));

    const int y0 = HINT_CY - HINT_R - 4;
    const int y1 = HINT_CY + HINT_R + 4;
    if (y0 < *bandY0) *bandY0 = y0;
    if (y1 > *bandY1) *bandY1 = y1;
}

// ------------------------------------------------------------------- boot ----
static void showSplash()
{
    gfx::clear(PX(RGB565(0x0B, 0x0B, 0x12)));
    gfx::strokeCircle(SCR_CX, SCR_CY, 150.0f, 2.0f, PX(RGB565(0x24, 0x24, 0x52)));
    gfx::drawSpriteSized(CLAWD_SPRITE, SCR_CX, SCR_CY - 8, -0.22f, 150.0f, CLAWD_SPRITE_PALETTE);
    gfx::textCentered("ESP32 BADGE", (int)SCR_CY + 82, PX(RGB565(0xD8, 0xD8, 0xE8)), 2);
    gfx::textCentered("clawd's playground", (int)SCR_CY + 108, PX(RGB565(0x5A, 0x5A, 0x72)), 1);
    LCD_pushFull(fb);
    LCD_waitTrans();
    delay(1100);
}

void setup()
{
    Serial.begin(115200);
    delay(150);
    Serial.println("\n[esp32-badge] booting");

    // The I2C bus has to be up before LCD_Init(), because LCD_Init() calls
    // Touch_Init(), which talks to the touch controller. Without this the controller
    // is reset by Touch_Init() and then left unaddressed.
    I2C_Init();
    Wire.setClock(400000);
    Serial.println("[sys] i2c bus up");

    randomSeed(esp_random());

    fb = (uint16_t *)heap_caps_malloc((size_t)SCR_W * SCR_H * 2, MALLOC_CAP_SPIRAM);
    if (!fb) {
        Serial.println("[esp32-badge] FATAL: framebuffer allocation failed");
        while (1) delay(1000);
    }
    Serial.printf("[sys] framebuffer at %p (%d bytes, PSRAM)\n", fb, SCR_W * SCR_H * 2);

    Backlight_Init();
    Serial.println("[sys] backlight init done");

    LCD_Init();
    Serial.println("[sys] LCD_Init done");

    Set_Backlight(85);
    Serial.printf("[sys] backlight set to %d\n", LCD_Backlight);

    input::init();
    Serial.println("[sys] input ready");

    // The level lives in the codec, which has no reset line on this board, so whatever
    // the last firmware left there is what is there now. Read it before touching it:
    // this line is the evidence for whether the assistant keeps its own idea of the
    // volume or inherits this one.
    Serial.printf("[audio] codec at boot: reg 0x32 = %d (%d%%)\n",
                  audio::rawRegister(), audio::volume());
    audio::applyStored();
    Serial.printf("[audio] after applying the saved level: reg 0x32 = %d (%d%%)\n",
                  audio::rawRegister(), audio::volume());

    // The first battery sample, so the launcher has something to draw immediately and
    // so a gauge that is not answering is visible in the log rather than only as "--"
    // on the screen.
    power::poll(0);
    const power::Reading batt = power::cached();
    Serial.printf("[batt] %s: %d%% %d mV\n", batt.valid ? "ok" : "not answering",
                  batt.percent, batt.millivolts);

    // Sound last, and only here: it needs the codec, which needs the I2C bus that is
    // already up, and a failed codec must not stop the launcher from starting.
    //
    sound::begin();

    // After the I2C bus is up, which the RTC shares with everything else.
    sysclock::begin();

    launcher::begin(GAMES, GAME_COUNT);

    showSplash();
    Serial.println("[sys] splash pushed");

    Serial.printf("[esp32-badge] %d screen(s) registered; launcher ready\n", GAME_COUNT);

}

void loop()
{
    static unsigned long lastMs = millis();
    const unsigned long now = millis();
    float dt = (now - lastMs) / 1000.0f;
    lastMs = now;
    if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;

    // A time from the host: "T<epoch>" then a newline on the serial line. This is how
    // the RTC gets set -- once, from the machine that knows what time it is -- rather
    // than the firmware bringing up a radio to ask.
    {
        static char cmd[24];
        static int n = 0;
        while (Serial.available()) {
            const char c = (char)Serial.read();
            if (c == 0x0A || c == 0x0D) {
                cmd[n] = 0;
                if (n > 1 && cmd[0] == 'T') {
                    const long long epoch = atoll(cmd + 1);
                    if (epoch > 1700000000) {
                        struct timeval tv = { (time_t)epoch, 0 };
                        settimeofday(&tv, nullptr);
                        sysclock::writeFromSystem();
                    }
                }
                n = 0;
            } else if (n < 23) {
                cmd[n++] = c;
            }
        }
    }

    input::update(dt);
    power::poll(now);      // the fuel gauge, at most every couple of seconds
    sound::update();       // keeps the I2S fed: silence when nothing is sounding
    sysclock::update(now); // fetches the time over wifi only if the RTC cannot supply it
    handleBootButton();

    int bandY0 = 0;
    int bandY1 = SCR_H - 1;

    if (screen == SCREEN_LAUNCHER) {
        Game *picked = launcher::update(dt);
        launcher::render();
        if (picked) {
            active = picked;
            active->enter();
            screen = SCREEN_GAME;
            Serial.printf("[sys] launching '%s'\n", active->name());
        }
    } else {
        active->update(dt);
        active->render();
        bandY0 = active->dirtyY0;
        bandY1 = active->dirtyY1;
    }

    drawHoldIndicator(input::bootHeldMs(),
                      (screen == SCREEN_GAME) ? HOLD_BACK_TO_LAUNCHER : HOLD_BACK_TO_STOCK,
                      &bandY0, &bandY1);

    LCD_pushBand(bandY0, bandY1, fb);
    LCD_waitTrans();
}
