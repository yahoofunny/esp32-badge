#include "game_xiaozhi.h"

#include <Arduino.h>
#include <math.h>

#include "Display_ST77916.h"
#include "arena.h"
#include "clawd_sprite.h"
#include "esp_ota_ops.h"
#include "gfx.h"

// Long enough for the message to be read, short enough not to feel like a hang.
static const float SWITCH_DELAY = 0.75f;

static const uint16_t COL_BG    = PX(RGB565(0x0B, 0x0B, 0x12));
static const uint16_t COL_TEXT  = PX(RGB565(0xE8, 0xE8, 0xF2));
static const uint16_t COL_DIM   = PX(RGB565(0x6A, 0x6A, 0x86));
static const uint16_t COL_WARN  = PX(RGB565(0xE0, 0x50, 0x50));
static const uint16_t COL_ACCENT = PX(RGB565(0x4A, 0x9E, 0xFF));

namespace xiaozhi {

bool switchToStockSystem()
{
    const esp_partition_t *ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!ota0) {
        Serial.println("[xiaozhi] no ota_0 partition -- nothing to switch to");
        return false;
    }

    // esp_ota_set_boot_partition() validates the image before writing, so a bad
    // slot is caught here rather than by an unbootable board.
    const esp_err_t err = esp_ota_set_boot_partition(ota0);
    if (err != ESP_OK) {
        Serial.printf("[xiaozhi] set_boot_partition failed: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.println("[xiaozhi] switching to the stock system; power cycle to come back");
    delay(120);
    esp_restart();
    return true;   // not reached
}

}  // namespace xiaozhi

void GameXiaoZhi::drawIcon(float cx, float cy, float size) const
{
    // Speech bubble: rounded body plus a tail, with three dots inside.
    const float w = size * 0.92f;
    const float h = size * 0.70f;
    gfx::fillRect((int)(cx - w * 0.5f), (int)(cy - h * 0.62f), (int)w, (int)h, COL_ACCENT);
    gfx::fillCircle(cx - w * 0.5f + 3.0f, cy - h * 0.62f + 3.0f, 4.0f, COL_ACCENT);
    gfx::fillCircle(cx + w * 0.5f - 3.0f, cy - h * 0.62f + 3.0f, 4.0f, COL_ACCENT);
    gfx::fillCircle(cx - w * 0.5f + 3.0f, cy - h * 0.62f + h - 3.0f, 4.0f, COL_ACCENT);
    gfx::fillCircle(cx + w * 0.5f - 3.0f, cy - h * 0.62f + h - 3.0f, 4.0f, COL_ACCENT);
    // tail
    for (int i = 0; i < (int)(h * 0.45f); i++) {
        gfx::fillRect((int)(cx - w * 0.18f + i * 0.35f), (int)(cy - h * 0.62f + h + i), 2, 1, COL_ACCENT);
    }
    // dots
    for (int i = -1; i <= 1; i++) {
        gfx::fillCircle(cx + i * size * 0.22f, cy - h * 0.62f + h * 0.5f, size * 0.075f, COL_BG);
    }
}

void GameXiaoZhi::enter()
{
    t = 0.0f;
    failed = false;
    dirtyY0 = 0;
    dirtyY1 = SCR_H - 1;
}

void GameXiaoZhi::update(float dt)
{
    if (failed) return;                 // stays on screen; BOOT returns to the launcher

    t += dt;
    if (t >= SWITCH_DELAY) {
        if (!xiaozhi::switchToStockSystem()) {
            failed = true;
        }
    }
}

void GameXiaoZhi::render()
{
    gfx::clear(COL_BG);
    gfx::strokeCircle(SCR_CX, SCR_CY, 120.0f, 2.0f, PX(RGB565(0x1E, 0x2A, 0x44)));

    if (failed) {
        gfx::textCentered("CANNOT SWITCH", (int)SCR_CY - 40, COL_WARN, 2);
        gfx::textCentered("the ota_0 slot is missing", (int)SCR_CY - 4, COL_DIM, 1);
        gfx::textCentered("or holds an invalid image", (int)SCR_CY + 14, COL_DIM, 1);
        gfx::textCentered("HOLD BOOT TO GO BACK", (int)SCR_CY + 56, COL_DIM, 1);
        setDirty(0, SCR_H - 1);
        return;
    }

    gfx::drawSpriteRotated(CLAWD_SPRITE, SCR_CX, SCR_CY - 34, 0.0f, 34.0f, CLAWD_SPRITE_PALETTE);
    gfx::textCentered("STARTING XIAOZHI", (int)SCR_CY + 28, COL_TEXT, 2);

    // Progress arc, so the brief wait reads as deliberate.
    const float p = t / SWITCH_DELAY;
    const int seg = 48;
    for (int i = 0; i < (int)(seg * p); i++) {
        const float a0 = -PI_F * 0.5f + (float)i / seg * 2.0f * PI_F;
        const float a1 = -PI_F * 0.5f + (float)(i + 1) / seg * 2.0f * PI_F;
        gfx::line(SCR_CX + cosf(a0) * 120.0f, SCR_CY + sinf(a0) * 120.0f,
                  SCR_CX + cosf(a1) * 120.0f, SCR_CY + sinf(a1) * 120.0f, COL_ACCENT);
    }

    gfx::textCentered("power cycle to come back", SCR_H - 30, COL_DIM, 1);
    setDirty(0, SCR_H - 1);
}
