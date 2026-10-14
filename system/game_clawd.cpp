#include "game_clawd.h"

#include <math.h>
#include <string.h>

#include "Display_ST77916.h"
#include "arena.h"
#include "clawd_sprite.h"
#include "assets_icons.h"
#include "gfx.h"
#include "input.h"

// ------------------------------------------------------------------ tuning ---
static const float ARENA    = 172.0f;   // arena radius, inner edge of the wall
static const float BALL_R   = 54.0f;    // clawd's circumradius on screen
static const float ACC_GAIN = 0.40f;    // feel knob; terminal speed ~= gain*tilt/(1-friction)
static const float FRICTION = 0.985f;
static const float MAX_SPEED = 9.0f;    // px per frame
static const float RESTITUTION = 0.86f;
static const float IMPACT_LIFE = 0.30f;

static const uint16_t COL_RING    = PX(RGB565(0x5A, 0x5A, 0xC8));
static const uint16_t COL_IMPACT  = PX(RGB565(0xFF, 0xD2, 0x3E));

// ------------------------------------------------------------------ setup ----
void GameClawd::drawIcon(float cx, float cy, float size) const
{
    // At a laptop. The three games used to share one standing pose, which made the
    // carousel three copies of the same silhouette with only the disc colour to tell
    // them apart; this one is a dark slab, unmistakable even at the back of the ring.
    gfx::drawSpriteSized(icon_roll, cx, cy, 0.0f, size, icon_roll_palette);
}

void GameClawd::enter()
{
    if (!backdrop) {
        backdrop = arena::build(COL_RING);
    }
    if (!backdrop) {
        Serial.println("[clawd] backdrop allocation failed");
        return;
    }

    bx = SCR_CX;
    by = SCR_CY - 90.0f;
    vx =  1.7f;                 // a lazy first push, so it is alive immediately
    vy = -1.2f;
    roll = 0.0f;
    impactAge = 0.0f;
    prevY0 = 0;
    prevY1 = SCR_H - 1;
    dirtyY0 = 0;
    dirtyY1 = SCR_H - 1;
}

void GameClawd::leave()
{
    if (backdrop) {
        free(backdrop);
        backdrop = nullptr;
    }
}

// ----------------------------------------------------------------- update ----
void GameClawd::update(float dt)
{
    const float tx = input::tiltX();
    const float ty = input::tiltY();

    vx += tx * ACC_GAIN;
    vy += ty * ACC_GAIN;
    vx *= FRICTION;
    vy *= FRICTION;

    const float sp = sqrtf(vx * vx + vy * vy);
    if (sp > MAX_SPEED) {
        vx *= MAX_SPEED / sp;
        vy *= MAX_SPEED / sp;
    }

    bx += vx;
    by += vy;

    // Circular wall: reflect about the radial normal.
    const float dx = bx - SCR_CX;
    const float dy = by - SCR_CY;
    const float d = sqrtf(dx * dx + dy * dy);
    const float lim = ARENA - BALL_R;

    if (d > lim && d > 0.001f) {
        const float nx = dx / d;
        const float ny = dy / d;
        bx = SCR_CX + nx * lim;
        by = SCR_CY + ny * lim;

        const float vn = vx * nx + vy * ny;
        if (vn > 0.0f) {
            vx -= (1.0f + RESTITUTION) * vn * nx;
            vy -= (1.0f + RESTITUTION) * vn * ny;
            impactX = SCR_CX + nx * ARENA;
            impactY = SCR_CY + ny * ARENA;
            impactAge = IMPACT_LIFE;
        }
    }

    // Angular speed proportional to linear speed. Not a physical model -- it is a
    // stylised cue that the thing is rolling.
    roll += sp / BALL_R;
    if (roll > 2.0f * PI_F) roll -= 2.0f * PI_F;

    if (impactAge > 0.0f) {
        impactAge -= dt;
        if (impactAge < 0.0f) impactAge = 0.0f;
    }
}

// ----------------------------------------------------------------- render ----
void GameClawd::render()
{
    if (!backdrop) return;

    const float ca = cosf(roll);
    const float sa = sinf(roll);

    int ny0 = (int)floorf(by - BALL_R); if (ny0 < 0) ny0 = 0;
    int ny1 = (int)ceilf (by + BALL_R); if (ny1 > SCR_H - 1) ny1 = SCR_H - 1;

    int uy0 = (ny0 < prevY0) ? ny0 : prevY0;
    int uy1 = (ny1 > prevY1) ? ny1 : prevY1;

    // impactWasVisible as well as impactAge: the ring is erased by painting its rows
    // back from the backdrop, and on the frame it expires impactAge is already zero.
    // Counting only the live ring leaves its outline on screen until the ball
    // happens to roll over those rows again.
    const bool ringVisible = impactAge > 0.0f;
    if (ringVisible || impactWasVisible) {
        int iy0 = (int)(impactY - 52); if (iy0 < 0) iy0 = 0;
        int iy1 = (int)(impactY + 52); if (iy1 > SCR_H - 1) iy1 = SCR_H - 1;
        if (iy0 < uy0) uy0 = iy0;
        if (iy1 > uy1) uy1 = iy1;
    }

    for (int y = uy0; y <= uy1; y++) {
        memcpy(fb + (size_t)y * SCR_W, backdrop + (size_t)y * SCR_W, SCR_W * 2);
    }

    gfx::drawSpriteRotated(CLAWD_SPRITE, bx, by, roll, BALL_R, CLAWD_SPRITE_PALETTE);

    if (ringVisible) {
        const float t = 1.0f - (impactAge / IMPACT_LIFE);
        const float r = 6.0f + t * 46.0f;
        const float thickness = 2.5f * (1.0f - t) + 0.9f;
        gfx::strokeCircle(impactX, impactY, r, thickness, COL_IMPACT);
    }

    prevY0 = ny0;
    prevY1 = ny1;
    impactWasVisible = ringVisible;
    setDirty(uy0, uy1);
}
