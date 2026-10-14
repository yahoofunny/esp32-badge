#include "game_settings.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>

#include "audio.h"
#include "gfx.h"
#include "input.h"

// ------------------------------------------------------------------ layout -----
static const int HEAD_Y   = 40;
static const int LABEL_Y  = 92;
static const int VALUE_Y  = 108;      // the big percentage
static const int VALUE_SCALE = 4;
static const int BAR_X0   = 64, BAR_X1 = 296;
static const int BAR_Y    = 178, BAR_H = 18;
static const int KNOB_R   = 13;
static const int REG_Y    = 208;      // the codec's raw register
static const int BTN_Y    = 262;
static const int BTN_A_X  = 92, BTN_B_X = 268;
static const int BTN_R    = 28;
static const int HINT_Y   = 316;

static const uint16_t COL_BG     = PX(RGB565(0x0E, 0x0E, 0x16));
static const uint16_t COL_TEXT   = PX(RGB565(0xEC, 0xEC, 0xF4));
static const uint16_t COL_DIM    = PX(RGB565(0x6E, 0x6E, 0x80));
static const uint16_t COL_ACCENT = PX(RGB565(0xE0, 0xA8, 0x50));
static const uint16_t COL_TRACK  = PX(RGB565(0x28, 0x28, 0x38));

// The two knobs. Big circles rather than arrow glyphs: the same lesson the pet's menu
// learned, that a word is not a touch target.
static float knobX(int percent)
{
    return BAR_X0 + (BAR_X1 - BAR_X0) * (float)percent / (float)audio::VOLUME_MAX;
}

void GameSettings::readCodec()
{
    raw = audio::rawRegister();
    present = raw >= 0;
    shown = audio::volume();
    if (shown < 0) shown = 0;
}

void GameSettings::apply(int percent)
{
    if (percent < audio::VOLUME_MIN) percent = audio::VOLUME_MIN;
    if (percent > audio::VOLUME_MAX) percent = audio::VOLUME_MAX;
    if (!audio::setVolume(percent)) {
        present = false;
        return;
    }
    // Read back rather than trusting the write: a codec that is not answering, or a
    // register that did not take, would otherwise look exactly like a level that was
    // set. The pet's save learned this the same way.
    present = audio::verify(percent);
    shown = percent;
    raw = audio::rawRegister();
    audio::storeVolume(percent);
    Serial.printf("[audio] volume -> %d%% (reg 0x32 = %d, %s)\n", percent, raw,
                  present ? "verified" : "NOT VERIFIED");
}

void GameSettings::enter()
{
    readCodec();
    dragging = false;
    Serial.printf("[audio] settings opened: codec reg 0x32 = %d (%d%%), %s\n",
                  raw, shown, present ? "present" : "NOT ANSWERING");
    setDirty(0, SCR_H - 1);
}

void GameSettings::leave()
{
    gfx::clear(COL_BG);
}

void GameSettings::update(float dt)
{
    (void)dt;
    const bool down = input::touchDown();

    // - and + first: their circles overlap the bar's row, and a tap inside one is a
    // nudge rather than a jump to that position.
    if (input::tapped()) {
        for (int i = 0; i < 2; i++) {
            const float bx = (i == 0) ? BTN_A_X : BTN_B_X;
            const float dx = (float)input::tapX() - bx;
            const float dy = (float)input::tapY() - BTN_Y;
            if (dx * dx + dy * dy <= (float)(BTN_R * BTN_R + 4)) {
                apply((shown < 0 ? 0 : shown) + (i ? audio::VOLUME_STEP : -audio::VOLUME_STEP));
                return;
            }
        }
    }

    // The bar itself: dragging it is the quick way to a level, a tap jumps to it.
    const float bx = knobX(shown < 0 ? 0 : shown);
    const float dxb = (float)input::tapX() - bx;
    const float dyb = (float)input::tapY() - (float)BAR_Y;
    const bool onKnob = dxb * dxb + dyb * dyb <= (float)((KNOB_R + 18) * (KNOB_R + 18));

    if (down && (dragging || onKnob)) {
        dragging = true;
    }
    if (!down) {
        dragging = false;
    }
    if (dragging && input::tapX() >= BAR_X0 - 30 && input::tapX() <= BAR_X1 + 30
        && input::tapY() >= BAR_Y - 44 && input::tapY() <= BAR_Y + 44) {
        int pct = (int)lroundf((input::tapX() - BAR_X0) * 100.0f / (BAR_X1 - BAR_X0));
        if (pct != shown) apply(pct);
    } else if (input::tapped()
               && input::tapX() >= BAR_X0 - 12 && input::tapX() <= BAR_X1 + 12
               && input::tapY() >= BAR_Y - 24 && input::tapY() <= BAR_Y + 24) {
        int pct = (int)lroundf((input::tapX() - BAR_X0) * 100.0f / (BAR_X1 - BAR_X0));
        apply(pct);
    }

    setDirty(0, SCR_H - 1);
}

void GameSettings::render()
{
    gfx::clear(COL_BG);
    gfx::textCentered("SETTINGS", HEAD_Y, COL_ACCENT, 1);

    gfx::textCentered("VOLUME", LABEL_Y, COL_DIM, 1);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", shown < 0 ? 0 : shown);
    gfx::textCentered(buf, VALUE_Y, present ? COL_TEXT : COL_DIM, VALUE_SCALE);
    if (!present) {
        gfx::textCentered("codec not answering", VALUE_Y + 40, COL_DIM, 1);
    }

    // The bar, and the knob on it. The filled part is the level, so the picture and
    // the number cannot disagree.
    const int y = BAR_Y;
    gfx::fillRect(BAR_X0, y, BAR_X1 - BAR_X0, BAR_H, COL_TRACK);
    const int fill = (int)((BAR_X1 - BAR_X0) * (shown < 0 ? 0 : shown) / audio::VOLUME_MAX);
    if (fill > 0) gfx::fillRect(BAR_X0, y, fill, BAR_H, gfx::dim(COL_ACCENT, 0.35f));
    for (int i = 0; i < 10; i++) {      // ticks, so a level is a number you can read off
        const int tx = BAR_X0 + (BAR_X1 - BAR_X0) * i / 10;
        gfx::fillRect(tx, y, 1, BAR_H, COL_BG);
    }
    gfx::fillCircle(knobX(shown < 0 ? 0 : shown), y + BAR_H / 2.0f, (float)KNOB_R, COL_ACCENT);
    gfx::fillCircle(knobX(shown < 0 ? 0 : shown), y + BAR_H / 2.0f, (float)(KNOB_R - 4),
                    gfx::dim(COL_ACCENT, 0.55f));

    // The register both firmwares are sharing. There is nowhere else to see it.
    snprintf(buf, sizeof(buf), "codec 0x32 = %d", raw);
    gfx::textCentered(buf, REG_Y, COL_DIM, 1);

    // - and +
    const int bx[2] = { BTN_A_X, BTN_B_X };
    for (int i = 0; i < 2; i++) {
        const uint16_t c = COL_ACCENT;
        gfx::strokeCircle((float)bx[i], (float)BTN_Y, (float)BTN_R, 2.0f, c);
        gfx::fillRect(bx[i] - 10, BTN_Y - 2, 21, 4, c);
        if (i == 1) gfx::fillRect(bx[i] - 2, BTN_Y - 10, 4, 21, c);
    }

    gfx::textCentered("shared with the assistant", HINT_Y, COL_DIM, 1);
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
