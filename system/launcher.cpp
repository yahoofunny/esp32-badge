#include "launcher.h"

#include <math.h>
#include <string.h>

#include "gfx.h"
#include "input.h"
#include "sound.h"

namespace launcher {

// ---------------------------------------------------------------- palette ----
static const uint16_t COL_BG     = PX(RGB565(0x0B, 0x0B, 0x12));
static const uint16_t COL_DOT    = PX(RGB565(0x18, 0x18, 0x2A));
static const uint16_t COL_TRACK  = PX(RGB565(0x22, 0x22, 0x46));
static const uint16_t COL_LABEL  = PX(RGB565(0xD8, 0xD8, 0xE8));
static const uint16_t COL_HINT   = PX(RGB565(0x5A, 0x5A, 0x72));

// ----------------------------------------------------------------- layout ----
// An ellipse rather than a circle: the vertical axis is squashed so the ring reads
// as a carousel turning away from the viewer instead of a flat clock face.
static const float ORBIT_X   = 112.0f;
static const float ORBIT_Y   = 66.0f;
static const float ICON_MIN  = 46.0f;    // footprint at the back of the ring
static const float ICON_MAX  = 74.0f;    // footprint at the front
static const float CENTER_R  = 46.0f;    // centre label exclusion radius

// One lean of the board moves exactly one place, then the step latches until the
// board is level again. Without the latch a held tilt free-runs through the list.
// One deliberate lean of the board moves exactly one place.
//
// Three things together, because thresholds alone are not enough to tell a lean
// apart from handling noise. The threshold is high enough to clear the wobble of
// simply holding the board (~21 degrees), the lean has to be *sustained* for a
// moment before it counts -- a wobble is momentary, a deliberate lean is not -- and
// the step latch only re-arms once the board is genuinely back near level (~10
// degrees). Without the release threshold being this high, a single wobble through
// the middle re-arms and fires several times.
static const float TILT_STEP    = 0.36f;  // g; ~21 degrees
static const float TILT_RELEASE = 0.17f;  // g; ~10 degrees
static const float TILT_DWELL   = 0.14f;  // seconds the lean must be held
static const float STEP_ANIM    = 9.0f;   // carousel easing rate, per second
static const float LAUNCH_TIME  = 0.34f;  // seconds

static Game *const *g_games = nullptr;
static int   g_count = 0;

static float selPos  = 0.0f;    // animated carousel position, in index units
// The selection is a running step count, deliberately NOT kept modulo the number of
// entries. Wrapping it here is what made the carousel animate backwards: stepping
// forward from the last entry to the first changed the target from n-1 to 0, so the
// easing difference went from +1 to -(n-1) and the ring span the long way round.
// Keeping it unbounded means selPos always travels in the direction of travel, and
// the modulo is applied only where an actual array index is needed.
static int   sel      = 0;
static bool  armed    = true;
static float leanHeld = 0.0f;   // seconds the current lean has been sustained
static float age      = 0.0f;   // seconds since the launcher appeared
static int   launching = -1;
static float launchT   = 0.0f;
static bool touchTracking = false, touchDragging = false, angleValid = false;
static bool touchEligible = false;
static int touchStartX = 0, touchStartY = 0;
static float touchAngle = 0, dragAngle = 0, dragStartPos = 0;
static float tiltCooldown = 0;

// Correct for the elliptical orbit before measuring rotation. Near the centre
// an angle is unstable, so crossing it temporarily pauses rotation.
static bool pointerAngle(int x, int y, float *angle)
{
    const float nx = (x - SCR_CX) / ORBIT_X, ny = (y - SCR_CY) / ORBIT_Y;
    const float r2 = nx * nx + ny * ny;
    if (r2 < 0.20f || r2 > 3.8f) return false;
    *angle = atan2f(ny, nx);
    return true;
}

// The step count is unbounded; this is where it becomes an array index.
static inline int selIndex()
{
    const int m = sel % g_count;
    return m < 0 ? m + g_count : m;
}

static float iconAngle(int i)
{
    const float step = 2.0f * PI_F / (float)g_count;
    return -PI_F * 0.5f + ((float)i - selPos) * step;
}

static void iconCentre(int i, float *x, float *y)
{
    const float a = iconAngle(i);
    *x = SCR_CX + ORBIT_X * cosf(a);
    *y = SCR_CY + ORBIT_Y * sinf(a);
}

// 1 at the front of the ring (top of the screen), 0 at the back.
static float frontness(float y)
{
    float t = 1.0f - (y - (SCR_CY - ORBIT_Y)) / (2.0f * ORBIT_Y);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return t;
}

void begin(Game *const *games, int count)
{
    g_games = games;
    g_count = count;
    selPos = 0.0f;
    sel = 0;
    armed = true;
    leanHeld = 0.0f;
    age = 0.0f;
    launching = -1;
    launchT = 0.0f;
    touchTracking = touchDragging = angleValid = touchEligible = false;
    tiltCooldown = 0;
}

static void step(int dir)
{
    sel += dir;   // unbounded on purpose -- see the note on `sel`
}

Game *update(float dt)
{
    age += dt;
    if (g_count <= 0) return nullptr;
    tiltCooldown = fmaxf(0, tiltCooldown - dt);

    bool draggedRelease = false;
    if (launching < 0) {
        if (input::touchDown()) {
            const int x = input::tapX(), y = input::tapY();
            float a = 0;
            const bool valid = pointerAngle(x, y, &a);
            if (!touchTracking) {
                touchTracking = true;
                touchDragging = false;
                touchStartX = x; touchStartY = y;
                dragStartPos = selPos; dragAngle = 0;
                touchEligible = valid;
                angleValid = valid; touchAngle = a;
            } else if (touchEligible) {
                if (valid && angleValid) {
                    float delta = a - touchAngle;
                    if (delta > PI_F) delta -= 2 * PI_F;
                    if (delta < -PI_F) delta += 2 * PI_F;
                    dragAngle += delta;
                    const float dx = x - touchStartX, dy = y - touchStartY;
                    if (dx * dx + dy * dy >= 100 && fabsf(dragAngle) > 0.05f)
                        touchDragging = true;
                    if (touchDragging) {
                        selPos = dragStartPos - dragAngle * g_count / (2 * PI_F);
                        sel = (int)lroundf(selPos);
                    }
                }
                angleValid = valid; touchAngle = a;
            }
            // Taps and hand motion must not trigger a simultaneous tilt step.
            armed = false; leanHeld = 0; tiltCooldown = 0.35f;
        } else if (touchTracking) {
            draggedRelease = touchDragging;
            if (touchDragging) sel = (int)lroundf(selPos);
            touchTracking = touchDragging = angleValid = touchEligible = false;
            tiltCooldown = 0.35f;
        }
    }

    if (launching < 0) {
        // --- tilt stepper ----------------------------------------------------
        // Requires a sustained lean, not just a reading over the threshold: the
        // accelerometer is noisy and a held board wobbles past any single-sample
        // limit, so the dwell is what separates intent from handling.
        const float tx = input::tiltX();
        if (!touchTracking && tiltCooldown <= 0 && armed) {
            if (fabsf(tx) > TILT_STEP) {
                leanHeld += dt;
                if (leanHeld >= TILT_DWELL) {
                    step(tx > 0 ? +1 : -1);
                    armed = false;
                    leanHeld = 0.0f;
                }
            } else {
                leanHeld = 0.0f;
            }
        } else if (!touchTracking && tiltCooldown <= 0 && fabsf(tx) < TILT_RELEASE) {
            armed = true;
            leanHeld = 0.0f;
        }

        // --- touch -----------------------------------------------------------
        if (input::tapped() && !draggedRelease) {
            const int txp = input::tapX(), typ = input::tapY();
            for (int i = 0; i < g_count; i++) {
                float ix, iy;
                iconCentre(i, &ix, &iy);
                const float half = 0.5f * (ICON_MIN + (ICON_MAX - ICON_MIN) * frontness(iy)) + 8.0f;
                const float dx = txp - ix, dy = typ - iy;
                if (dx * dx + dy * dy <= half * half) {
                    // Re-base rather than assign, so the carousel eases to the
                    // tapped entry by the shortest route instead of unwinding.
                    const bool wasSelected = (i == selIndex());
                    sel += i - selIndex();
                    launching = i;
                    launchT = 0.0f;
                    sound::play2(wasSelected ? 880 : 660, 30, wasSelected ? 1320 : 880, 45);
                    return nullptr;
                }
            }
            // A tap near the middle launches whatever is selected, so the carousel
            // can be driven purely by tilt and then confirmed with one tap.
            const float cdx = txp - SCR_CX, cdy = typ - SCR_CY;
            if (cdx * cdx + cdy * cdy <= CENTER_R * CENTER_R) {
                launching = selIndex();
                launchT = 0.0f;
                sound::launch();
                return nullptr;
            }
        }
    } else {
        launchT += dt / LAUNCH_TIME;
        if (launchT >= 1.0f) {
            Game *g = g_games[launching];
            launching = -1;
            launchT = 0.0f;
            return g;
        }
    }

    // Ease the carousel toward the selection.
    const float diff = (float)sel - selPos;
    if (!touchTracking) selPos += diff * fminf(1.0f, STEP_ANIM * dt);

    return nullptr;
}

// --------------------------------------------------------------- drawing -----
static void drawBackdrop()
{
    gfx::clear(COL_BG);

    // Sparse floor dots, same lattice as the games, so the system feels of a piece.
    for (int y = 22; y < SCR_H; y += 45) {
        for (int x = 22; x < SCR_W; x += 45) {
            gfx::fillRect(x, y, 2, 2, COL_DOT);
        }
    }

    // The track the icons ride on, drawn as short segments between sampled points
    // so it follows the ellipse rather than approximating it with a circle.
    const int SEG = 96;
    float px = 0, py = 0;
    for (int i = 0; i <= SEG; i++) {
        const float a = (float)i / SEG * 2.0f * PI_F;
        const float x = SCR_CX + ORBIT_X * cosf(a);
        const float y = SCR_CY + ORBIT_Y * sinf(a);
        if (i) gfx::line(px, py, x, y, COL_TRACK);
        px = x;
        py = y;
    }
}

static void drawIconAt(int i, float scaleBoost)
{
    float cx, cy;
    iconCentre(i, &cx, &cy);

    const float t = frontness(cy);
    const float base = ICON_MIN + (ICON_MAX - ICON_MIN) * t;
    const float size = base * scaleBoost;

    // A soft disc behind the art gives every entry the same silhouette and reads as
    // the touch target, which the hit test above matches.
    const uint16_t accent = g_games[i]->accent();
    gfx::fillCircle(cx, cy, size * 0.5f, gfx::dim(accent, 0.72f - 0.42f * t));
    gfx::strokeCircle(cx, cy, size * 0.5f, 2.0f, gfx::mix(COL_BG, accent, 0.35f + 0.65f * t));

    g_games[i]->drawIcon(cx, cy, size * 0.56f);

    if (i == selIndex() && launching < 0) {
        // Breathing ring marks the selection without adding any text.
        const float pulse = 0.5f + 0.5f * sinf(age * 3.2f);
        gfx::strokeCircle(cx, cy, size * 0.5f + 3.0f + pulse * 1.5f, 2.0f,
                          gfx::mix(accent, PX(RGB565(0xFF, 0xFF, 0xFF)), 0.35f));
    }
}


void render()
{
    if (g_count <= 0) return;

    drawBackdrop();

    // Painter's order: back of the ring first, so the front entries overlap them.
    int order[16];
    int n = g_count > 16 ? 16 : g_count;
    for (int i = 0; i < n; i++) order[i] = i;
    for (int a = 0; a < n - 1; a++) {
        for (int b = a + 1; b < n; b++) {
            float ax, ay, bx, by;
            iconCentre(order[a], &ax, &ay);
            iconCentre(order[b], &bx, &by);
            if (by < ay) { const int tmp = order[a]; order[a] = order[b]; order[b] = tmp; }
        }
    }

    float selBoost = 1.0f;
    if (launching >= 0) selBoost = 1.0f + launchT * 1.1f;

    for (int k = 0; k < n; k++) {
        const int i = order[k];
        drawIconAt(i, (i == launching) ? selBoost : 1.0f);
    }

    // Keep the orbit as the focus: selected name and a quiet gesture hint only.
    if (launching < 0) {
        gfx::textCentered(g_games[selIndex()]->name(), (int)(SCR_CY + ORBIT_Y) + 34, COL_LABEL, 2);
        // Kept short on purpose. Only about 176 of the 360 px of width are visible
        // at this height, and the earlier longer wording ("tilt to browse - tap to
        // play", 209 px) had its ends hidden behind the bezel.
        gfx::textCentered(g_count > 1 ? "drag ring - tap" : "tap to play",
                          330, COL_HINT, 1);
    } else {
        gfx::textCentered(g_games[launching]->name(), SCR_H - 30, COL_LABEL, 2);
    }
}

}  // namespace launcher
