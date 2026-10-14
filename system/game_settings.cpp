#include "game_settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "gfx.h"
#include "input.h"
#include "power.h"

// ------------------------------------------------------------------ layout -----
static const int HEAD_Y   = 30;       // the page title and the way back
static const int BACK_Y   = 30;
static const int BACK_X0  = 100, BACK_X1 = 260;   // its grab area, and its label
static const int ROW_Y0   = 70;
static const int ROW_PITCH = 19;
static const int COL_LABEL = 64;      // a fixed label column, so the list reads as one
static const int COL_VALUE = 120;     // labels are at most eight characters, then this

// The volume page's own geometry, unchanged from when it was the whole app.
static const int LABEL_Y  = 92;
static const int VALUE_Y  = 108;
static const int VALUE_SCALE = 4;
static const int BAR_X0   = 64, BAR_X1 = 296;
static const int BAR_Y    = 178, BAR_H = 18;
static const int KNOB_R   = 13;
static const int REG_Y    = 208;
static const int BTN_Y    = 262;
static const int BTN_A_X  = 92, BTN_B_X = 268;
static const int BTN_R    = 28;
static const int HINT_Y   = 316;

// The menu: three items on an arc, the shape this round screen keeps asking for.
static const float MENU_R    = 118.0f;
static const float MENU_SPAN = 88.0f;
static const float MENU_HIT  = 34.0f;

static const uint16_t COL_BG     = PX(RGB565(0x0E, 0x0E, 0x16));
static const uint16_t COL_TEXT   = PX(RGB565(0xEC, 0xEC, 0xF4));
static const uint16_t COL_DIM    = PX(RGB565(0x6E, 0x6E, 0x80));
static const uint16_t COL_ACCENT = PX(RGB565(0xE0, 0xA8, 0x50));
static const uint16_t COL_TRACK  = PX(RGB565(0x28, 0x28, 0x38));

static const char *const MENU_LABEL[3] = { "VOLUME", "SYSTEM", "NET" };

static float knobX(int percent)
{
    return BAR_X0 + (BAR_X1 - BAR_X0) * (float)percent / (float)audio::VOLUME_MAX;
}

void GameSettings::menuSlot(int i, float *sx, float *sy) const
{
    const float step = MENU_SPAN / 2.0f;
    const float a = (-MENU_SPAN * 0.5f + step * (float)i) * PI_F / 180.0f;
    *sx = SCR_CX + sinf(a) * MENU_R;
    *sy = SCR_CY + cosf(a) * MENU_R;
}

// ------------------------------------------------------------------ volume ------
void GameSettings::refresh()
{
    shown = audio::assistantVolume();
    raw = audio::rawRegister();
}

void GameSettings::apply(int percent)
{
    if (percent < audio::ASSISTANT_MIN) percent = audio::ASSISTANT_MIN;
    if (percent > audio::VOLUME_MAX) percent = audio::VOLUME_MAX;

    // Two writes for two different readers. The NVS key is what the assistant picks up
    // when it next starts -- that is the one that matters. The codec register is for
    // anything of ours that would make a sound before then; the two use different
    // scales, so each gets its own call rather than one being derived from the other.
    //
    // Both are read back rather than trusted: a write that reports success and a value
    // that is not there look identical until something reads it. The pet's save taught
    // this the same way.
    saved = audio::setAssistantVolume(percent);
    audio::storeVolume(percent);
    audio::setVolume(percent);
    audio::verify(percent);
    shown = percent;
    raw = audio::rawRegister();
    Serial.printf("[audio] volume -> %d%% (nvs %s, codec reg 0x32 = %d)\n", percent,
                  saved ? "verified" : "NOT SAVED", raw);
}

// ------------------------------------------------------------- lifecycle --------
void GameSettings::enterPage(Page p)
{
    page = p;
    dragging = false;
    setDirty(0, SCR_H - 1);
}

void GameSettings::enter()
{
    refresh();
    saved = true;
    enterPage(PAGE_MENU);
    Serial.printf("[audio] settings opened: assistant level %d%% (nvs), "
                  "codec reg 0x32 = %d\n", shown, raw);
}

void GameSettings::leave()
{
    gfx::clear(COL_BG);
}

// ------------------------------------------------------------------ input -------
static bool inRect(int x, int y, int x0, int y0, int x1, int y1)
{
    return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

void GameSettings::updateMenu()
{
    for (int i = 0; i < 3; i++) {
        float sx, sy;
        menuSlot(i, &sx, &sy);
        const float dx = (float)input::tapX() - sx;
        const float dy = (float)input::tapY() - sy;
        if (input::tapped() && dx * dx + dy * dy <= MENU_HIT * MENU_HIT) {
            enterPage((Page)(PAGE_VOLUME + i));
            return;
        }
    }
}

void GameSettings::updateVolume()
{
    // The way back first: its strip is clear of everything else on this page.
    if (input::tapped() && inRect(input::tapX(), input::tapY(), BACK_X0, 14, BACK_X1, 48)) {
        enterPage(PAGE_MENU);
        return;
    }

    const bool down = input::touchDown();

    // - and + next: their circles overlap the bar's rows, and a tap inside one is a
    // nudge rather than a jump to that position.
    if (input::tapped()) {
        for (int i = 0; i < 2; i++) {
            const float bx = (i == 0) ? BTN_A_X : BTN_B_X;
            const float dx = (float)input::tapX() - bx;
            const float dy = (float)input::tapY() - BTN_Y;
            if (dx * dx + dy * dy <= (float)(BTN_R * BTN_R + 4)) {
                apply((shown < 0 ? 0 : shown)
                      + (i ? audio::VOLUME_STEP : -audio::VOLUME_STEP));
                return;
            }
        }
    }

    // The bar: dragging is the quick way to a level, a tap jumps to it.
    const float bx = knobX(shown < 0 ? 0 : shown);
    const float dxb = (float)input::tapX() - bx;
    const float dyb = (float)input::tapY() - (float)BAR_Y;
    const bool onKnob = dxb * dxb + dyb * dyb <= (float)((KNOB_R + 18) * (KNOB_R + 18));

    if (down && (dragging || onKnob)) dragging = true;
    if (!down) dragging = false;

    if (dragging && inRect(input::tapX(), input::tapY(), BAR_X0 - 30, BAR_Y - 44,
                           BAR_X1 + 30, BAR_Y + 44)) {
        const int pct = (int)lroundf((input::tapX() - BAR_X0) * 100.0f / (BAR_X1 - BAR_X0));
        if (pct != shown) apply(pct);
    } else if (input::tapped()
               && inRect(input::tapX(), input::tapY(), BAR_X0 - 12, BAR_Y - 24,
                         BAR_X1 + 12, BAR_Y + 24)) {
        apply((int)lroundf((input::tapX() - BAR_X0) * 100.0f / (BAR_X1 - BAR_X0)));
    }
}

void GameSettings::updateInfo()
{
    if (input::tapped() && inRect(input::tapX(), input::tapY(), BACK_X0, 14, BACK_X1, 48)) {
        enterPage(PAGE_MENU);
    }
}

void GameSettings::update(float dt)
{
    (void)dt;
    switch (page) {
    case PAGE_MENU:   updateMenu(); break;
    case PAGE_VOLUME: updateVolume(); break;
    default:          updateInfo(); break;
    }
    setDirty(0, SCR_H - 1);
}

// ----------------------------------------------------------------- drawing ------
static void row(int i, const char *label, const char *value, uint16_t colour)
{
    const int y = ROW_Y0 + i * ROW_PITCH;
    gfx::text(label, COL_LABEL, y, COL_DIM, 1);
    gfx::text(value, COL_VALUE, y, colour, 1);
}

void GameSettings::renderBack()
{
    const char *label = "BACK";
    const int w = gfx::textWidth(label, 1);
    gfx::text(label, (int)(SCR_CX - w * 0.5f), BACK_Y, COL_ACCENT, 1);
}

void GameSettings::renderMenu()
{
    gfx::clear(COL_BG);
    gfx::textCentered("SETTINGS", HEAD_Y, COL_ACCENT, 1);

    for (int i = 0; i < 3; i++) {
        float sx, sy;
        menuSlot(i, &sx, &sy);
        gfx::strokeCircle(sx, sy, MENU_HIT * 0.88f, 1.0f, gfx::dim(COL_ACCENT, 0.45f));
        const int w = gfx::textWidth(MENU_LABEL[i], 1);
        gfx::text(MENU_LABEL[i], (int)(sx - w * 0.5f), (int)sy - 4, COL_ACCENT, 1);
    }

    const power::Reading b = power::cached();
    char line[32];
    if (b.valid) snprintf(line, sizeof(line), "battery %d%%", b.percent);
    else         snprintf(line, sizeof(line), "battery --");
    gfx::textCentered(line, 62, b.valid && b.percent < 20 ? PX(RGB565(0xE0, 0x50, 0x50))
                                                          : COL_DIM, 1);
}

void GameSettings::renderVolume()
{
    gfx::clear(COL_BG);
    renderBack();

    gfx::textCentered("VOLUME", LABEL_Y, COL_DIM, 1);

    char buf[24];
    snprintf(buf, sizeof(buf), "%d%%", shown < 0 ? 0 : shown);
    gfx::textCentered(buf, VALUE_Y, COL_TEXT, VALUE_SCALE);
    if (!saved) {
        gfx::textCentered("could not save", VALUE_Y + 40, COL_DIM, 1);
    }

    const int y = BAR_Y;
    gfx::fillRect(BAR_X0, y, BAR_X1 - BAR_X0, BAR_H, COL_TRACK);
    const int fill = (int)((BAR_X1 - BAR_X0) * (shown < 0 ? 0 : shown) / audio::VOLUME_MAX);
    if (fill > 0) gfx::fillRect(BAR_X0, y, fill, BAR_H, gfx::dim(COL_ACCENT, 0.35f));
    for (int i = 0; i < 10; i++) {
        gfx::fillRect(BAR_X0 + (BAR_X1 - BAR_X0) * i / 10, y, 1, BAR_H, COL_BG);
    }
    gfx::fillCircle(knobX(shown < 0 ? 0 : shown), y + BAR_H / 2.0f, (float)KNOB_R, COL_ACCENT);
    gfx::fillCircle(knobX(shown < 0 ? 0 : shown), y + BAR_H / 2.0f, (float)(KNOB_R - 4),
                    gfx::dim(COL_ACCENT, 0.55f));

    // Not the control -- the evidence. This is the register the assistant writes from
    // the level above, on its own scale, so after it has booted the two numbers move
    // together and this one shows whether the level reached it.
    snprintf(buf, sizeof(buf), "codec 0x32 = %d", raw);
    gfx::textCentered(buf, REG_Y, COL_DIM, 1);

    const int bx[2] = { BTN_A_X, BTN_B_X };
    for (int i = 0; i < 2; i++) {
        gfx::strokeCircle((float)bx[i], (float)BTN_Y, (float)BTN_R, 2.0f, COL_ACCENT);
        gfx::fillRect(bx[i] - 10, BTN_Y - 2, 21, 4, COL_ACCENT);
        if (i == 1) gfx::fillRect(bx[i] - 2, BTN_Y - 10, 4, 21, COL_ACCENT);
    }

    gfx::textCentered("assistant reads this at boot", HINT_Y, COL_DIM, 1);
}

void GameSettings::renderSystem()
{
    gfx::clear(COL_BG);
    renderBack();

    char v[10][28];

    snprintf(v[0], sizeof(v[0]), "%s r%d", ESP.getChipModel(), (int)ESP.getChipRevision());
    snprintf(v[1], sizeof(v[1]), "%d @ %d MHz", (int)ESP.getChipCores(),
             (int)ESP.getCpuFreqMHz());
    snprintf(v[2], sizeof(v[2]), "%u MB", (unsigned)(ESP.getFlashChipSize() >> 20));
    snprintf(v[3], sizeof(v[3]), "%u MB", (unsigned)(ESP.getPsramSize() >> 20));
    snprintf(v[4], sizeof(v[4]), "%u KB free", (unsigned)(ESP.getFreeHeap() >> 10));
    snprintf(v[5], sizeof(v[5]), "%u KB", (unsigned)(ESP.getSketchSize() >> 10));
    snprintf(v[6], sizeof(v[6]), "%s", __DATE__);

    const uint32_t secs = millis() / 1000;
    snprintf(v[7], sizeof(v[7]), "%uh %um", (unsigned)(secs / 3600), (unsigned)((secs / 60) % 60));

    const power::Reading b = power::cached();
    if (b.valid) snprintf(v[8], sizeof(v[8]), "%d%%  %d mV", b.percent, b.millivolts);
    else         snprintf(v[8], sizeof(v[8]), "not present");

    const esp_partition_t *run = esp_ota_get_running_partition();
    snprintf(v[9], sizeof(v[9]), "%s", run ? run->label : "?");

    row(0, "chip",    v[0], COL_TEXT);
    row(1, "cores",   v[1], COL_TEXT);
    row(2, "flash",   v[2], COL_TEXT);
    row(3, "psram",   v[3], COL_TEXT);
    row(4, "heap",    v[4], COL_TEXT);
    row(5, "sketch",  v[5], COL_TEXT);
    row(6, "build",   v[6], COL_TEXT);
    row(7, "uptime",  v[7], COL_TEXT);
    row(8, "battery", v[8], b.valid && b.percent < 20 ? PX(RGB565(0xE0, 0x50, 0x50))
                                                     : COL_TEXT);
    row(9, "running", v[9], COL_TEXT);
}

void GameSettings::renderNet()
{
    gfx::clear(COL_BG);
    renderBack();

    // The assistant's settings, from its own NVS. Nothing is shown that would be a
    // secret: the password's presence is reported, never its value.
    Preferences prefs;
    char ssid[28] = "not set";
    bool key = false;
    char server[28] = "not set";
    char uuid[28] = "not set";

    if (prefs.begin("wifi", true)) {
        if (prefs.isKey("ssid")) prefs.getString("ssid", ssid, sizeof(ssid));
        key = prefs.isKey("password");
        prefs.end();
    }
    if (prefs.begin("mqtt", true)) {
        if (prefs.isKey("endpoint")) prefs.getString("endpoint", server, sizeof(server));
        prefs.end();
    }
    if (prefs.begin("board", true)) {
        if (prefs.isKey("uuid")) {
            char full[40] = "";
            prefs.getString("uuid", full, sizeof(full));
            full[8] = 0;                       // the front is enough to identify it
            snprintf(uuid, sizeof(uuid), "%s", full);
        }
        prefs.end();
    }

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macText[24];
    snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    row(0, "wifi",   ssid,          strcmp(ssid, "not set") ? COL_TEXT : COL_DIM);
    row(1, "key",    key ? "set" : "missing", key ? COL_TEXT : COL_DIM);
    row(2, "mac",    macText,       COL_TEXT);
    row(3, "server", server,        strcmp(server, "not set") ? COL_TEXT : COL_DIM);
    row(4, "device", uuid,          COL_TEXT);

    // Said plainly, because the two lines above could otherwise read as this system's
    // own connection. It has none: the assistant in the other slot is what talks to
    // that server, and only while it is the one running.
    gfx::textCentered("the assistant's, not ours", ROW_Y0 + 8 * ROW_PITCH, COL_DIM, 1);
}

void GameSettings::render()
{
    switch (page) {
    case PAGE_MENU:   renderMenu(); break;
    case PAGE_VOLUME: renderVolume(); break;
    case PAGE_SYSTEM: renderSystem(); break;
    default:          renderNet(); break;
    }
}

void GameSettings::drawIcon(float cx, float cy, float size) const
{
    // The sliders glyph: three tracks with a knob on each, which is what settings look
    // like everywhere. Not one of clawd's poses -- this is not clawd.
    const int w = (int)(size * 0.86f);
    const int x0 = (int)cx - w / 2;
    const int h = (int)fmaxf(2.0f, size * 0.075f);
    const int r = (int)fmaxf(2.0f, size * 0.10f);
    const float offs[3] = { 0.68f, 0.30f, 0.55f };
    for (int i = 0; i < 3; i++) {
        const int y = (int)cy + (i - 1) * (int)(size * 0.27f);
        gfx::fillRect(x0, y - h / 2, w, h, COL_ACCENT);
        gfx::fillCircle(x0 + w * offs[i], (float)y, (float)r, COL_BG);
        gfx::fillCircle(x0 + w * offs[i], (float)y, (float)(r - 2), COL_ACCENT);
    }
}
