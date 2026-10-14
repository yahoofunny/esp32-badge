#include "game_tap.h"

#include <math.h>
#include <string.h>

#include "Display_ST77916.h"
#include "arena.h"
#include "clawd_sprite.h"
#include "assets_icons.h"
#include "gfx.h"
#include "input.h"

static const float TAP_R      = 46.0f;   // clawd's circumradius when it pops up
static const float SPAWN_IN   = 12.0f;   // px margin kept from the wall
static const float LIFE_START = 1.45f;   // seconds on screen for the first clawd
static const float LIFE_MIN   = 0.55f;   // floor, however high the score climbs
static const float LIFE_STEP  = 0.06f;   // shaved off per hit
static const int   MAX_MISSES = 3;

static const uint16_t COL_RING    = PX(RGB565(0x2E, 0xC4, 0x6B));
static const uint16_t COL_TEXT    = PX(RGB565(0xE8, 0xE8, 0xF2));
static const uint16_t COL_DIM     = PX(RGB565(0x8A, 0x8A, 0x9A));
static const uint16_t COL_PANEL   = PX(RGB565(0x12, 0x12, 0x1E));
static const uint16_t COL_FLASH   = PX(RGB565(0xFF, 0xFF, 0xFF));

void GameTap::drawIcon(float cx, float cy, float size) const
{
    // The idea bulb: clawd appearing with a light on, which is what the game is --
    // he pops up somewhere and you have to spot him.
    gfx::drawSpriteSized(icon_tap, cx, cy, 0.0f, size, icon_tap_palette);
}

void GameTap::enter()
{
    if (!backdrop) {
        backdrop = arena::build(COL_RING);
    }
    over = false;
    score = 0;
    misses = 0;
    hitFlash = 0.0f;
    prevY0 = 0;
    prevY1 = SCR_H - 1;
    dirtyY0 = 0;
    dirtyY1 = SCR_H - 1;
    spawn();
}

void GameTap::leave()
{
    if (backdrop) {
        free(backdrop);
        backdrop = nullptr;
    }
}

void GameTap::spawn()
{
    // Uniform-ish placement inside the wall: pick an angle and a radius that keeps
    // the sprite clear of the ring, so the touch target is never half off-screen.
    const float a = (float)random(0, 3600) * 0.001f;
    const float maxR = 172.0f - TAP_R - SPAWN_IN;
    const float r = sqrtf((float)random(0, 1000) * 0.001f) * maxR;

    tx = SCR_CX + cosf(a) * r;
    ty = SCR_CY + sinf(a) * r;

    lifeMax = LIFE_START - score * LIFE_STEP;
    if (lifeMax < LIFE_MIN) lifeMax = LIFE_MIN;
    life = lifeMax;
    pop = 0.0f;
}

void GameTap::update(float dt)
{
    if (hitFlash > 0.0f) {
        hitFlash -= dt * 4.0f;
        if (hitFlash < 0.0f) hitFlash = 0.0f;
    }

    if (over) {
        if (input::tapped()) {
            over = false;
            score = 0;
            misses = 0;
            spawn();
        }
        return;
    }

    if (pop < 1.0f) {
        pop += dt * 6.0f;
        if (pop > 1.0f) pop = 1.0f;
    }

    if (input::tapped()) {
        const float dx = input::tapX() - tx;
        const float dy = input::tapY() - ty;
        // Slightly tighter than the full circumradius: clawd is wider than it is
        // tall, so a circular test at full radius would accept taps in the corners
        // where there is visibly nothing.
        const float hit = TAP_R * 0.85f;
        if (dx * dx + dy * dy <= hit * hit) {
            score++;
            if (score > best) best = score;
            hitFlash = 1.0f;
            spawn();
            return;
        }
    }

    life -= dt;
    if (life <= 0.0f) {
        misses++;
        if (misses >= MAX_MISSES) {
            over = true;
        } else {
            spawn();
        }
    }
}

void GameTap::render()
{
    if (!backdrop) return;

    int ny0, ny1;
    if (over) {
        // The overlay is drawn on top of the whole field, so everything is dirty.
        ny0 = 0;
        ny1 = SCR_H - 1;
    } else {
        ny0 = (int)floorf(ty - TAP_R * 1.6f); if (ny0 < 0) ny0 = 0;
        ny1 = (int)ceilf (ty + TAP_R * 1.6f); if (ny1 > SCR_H - 1) ny1 = SCR_H - 1;
        // The HUD strip at the top is repainted every frame too.
        if (ny0 > 6) { /* keep the band contiguous from the HUD down */ }
    }

    int uy0 = (ny0 < prevY0) ? ny0 : prevY0;
    int uy1 = (ny1 > prevY1) ? ny1 : prevY1;
    if (!over) {
        // Always include the HUD rows: the score changes there.
        if (uy0 > 4) uy0 = 4;
    }

    arena::restore(backdrop, uy0, uy1);

    if (!over) {
        const float scale = 0.5f + 0.5f * pop;          // grow in
        const uint16_t body = (hitFlash > 0.0f)
                                  ? gfx::mix(CLAWD_SPRITE_PALETTE[1], COL_FLASH, hitFlash * 0.8f)
                                  : CLAWD_SPRITE_PALETTE[1];
        const uint16_t pal[4] = { 0, body, CLAWD_SPRITE_PALETTE[2], body };
        gfx::drawSpriteRotated(CLAWD_SPRITE, tx, ty, 0.0f, TAP_R * scale, pal);

        // Shrinking timer bar under the score, so the deadline is legible.
        const float frac = lifeMax > 0.0f ? (life / lifeMax) : 0.0f;
        const int barW = (int)(120.0f * frac);
        gfx::fillRect((int)SCR_CX - 60, 30, 120, 5, PX(RGB565(0x22, 0x22, 0x33)));
        gfx::fillRect((int)SCR_CX - 60, 30, barW, 5, COL_RING);

        char buf[24];
        snprintf(buf, sizeof(buf), "%d", score);
        gfx::textCentered(buf, 8, COL_TEXT, 2);

        // Misses as pips. These used to sit against the right edge, where the round
        // bezel hid them completely: at y=46 only about x=60..300 is visible, so the
        // player could never see how many lives were left.
        for (int i = 0; i < MAX_MISSES; i++) {
            const uint16_t c = (i < misses) ? PX(RGB565(0xE0, 0x50, 0x50))
                                            : PX(RGB565(0x2A, 0x2A, 0x3A));
            gfx::fillCircle(262.0f + i * 9.0f, 46.0f, 3.0f, c);
        }
    } else {
        gfx::fillCircle(SCR_CX, SCR_CY, 104.0f, COL_PANEL);
        gfx::strokeCircle(SCR_CX, SCR_CY, 104.0f, 2.0f, gfx::dim(COL_RING, 0.3f));

        gfx::textCentered("TIME UP", (int)SCR_CY - 52, COL_TEXT, 2);

        char buf[32];
        snprintf(buf, sizeof(buf), "SCORE %d", score);
        gfx::textCentered(buf, (int)SCR_CY - 14, COL_RING, 2);

        snprintf(buf, sizeof(buf), "BEST %d", best);
        gfx::textCentered(buf, (int)SCR_CY + 18, COL_DIM, 1);

        gfx::textCentered("TAP TO PLAY AGAIN", (int)SCR_CY + 50, COL_DIM, 1);
    }

    prevY0 = ny0;
    prevY1 = ny1;
    setDirty(uy0, uy1);
}
