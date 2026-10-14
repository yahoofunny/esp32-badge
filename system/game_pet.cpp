#include "game_pet.h"

#include <Arduino.h>
#include <Preferences.h>
#include "esp_random.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Display_ST77916.h"
#include "assets_pet.h"
#include "gfx.h"
#include "input.h"

// --------------------------------------------------------------- constants ---
// Straight from the original: same thresholds, same rates, so the ported pet ages
// and suffers at the same pace as the one on the blog.
static const float HATCH_MIN = 3.0f;
static const float TEEN_MIN  = 60.0f;
static const float ADULT_MIN = 360.0f;

static const char *STAGE_NAMES[4] = { "EGG", "BABY", "TEEN", "ADULT" };

static const float R_HUNGER = 1.5f;    // per minute, awake and well
static const float R_FUN    = 1.2f;
static const float R_CLEAN  = 0.9f;
static const float R_POOP   = 2.0f;    // extra dirt per minute while a poop is present
static const float R_ENERGY = 1.1f;
static const float R_SLEEP_ENERGY = 8.0f;
static const float SICK_MULT = 1.5f;
static const float SLEEP_MULT = 0.5f;

static const uint32_t RUNAWAY_HUNGER_SEC = 120UL * 60;
static const uint32_t RUNAWAY_CRIT_SEC   = 60UL * 60;

static const float POOP_CHANCE_PER_MIN = 0.05f;

static const uint32_t SAVE_PERIOD_MS = 15000;

// Upper bound on the decay applied for time spent off-screen. The original allows 48
// hours because its clock runs while the browser is closed; ours only runs while the
// badge is powered, and you are usually holding it, so a smaller cap keeps an
// overnight stay in the launcher from being a death sentence.
static const uint32_t AWAY_CAP_SEC = 4UL * 3600;

// Identifies this power-on. Regenerated lazily so it is stable for the whole session.
static uint32_t petSessionId = 0;
static const uint32_t MSG_LINGER_MS  = 6000;

static const char *NVS_NAMESPACE = "clawdpet";
static const uint32_t SAVE_MAGIC = 0x50544543;   // 'CETP'

// ------------------------------------------------------------------ layout ---
static const int BAR_X = 124, BAR_W = 116, BAR_H = 7;
static const int BAR_Y0 = 30, BAR_PITCH = 14;

static const float PET_CX = 180.0f, PET_CY = 168.0f;
static const float PET_SCALE = 6.0f;

// Clear of the button row below it (which starts at BTN_ROW1_Y - BTN_R = 248) and
// clear of the pet above, which reaches PET_CY + 9*PET_SCALE/2.
static const int MSG_Y = 234;

static const float BTN_R = 20.0f;
static const float BTN_ROW1_Y = 268.0f;
static const float BTN_ROW2_Y = 310.0f;
static const float BTN_PITCH = 46.0f;

// ------------------------------------------------------------------ colours ---
static const uint16_t COL_BG     = PX(RGB565(0x0B, 0x0B, 0x12));
static const uint16_t COL_DOT    = PX(RGB565(0x18, 0x18, 0x2A));
static const uint16_t COL_TEXT   = PX(RGB565(0xD8, 0xD8, 0xE8));
static const uint16_t COL_DIM    = PX(RGB565(0x6A, 0x6A, 0x86));
static const uint16_t COL_TRACK  = PX(RGB565(0x20, 0x20, 0x32));
static const uint16_t COL_WARN   = PX(RGB565(0xE0, 0x50, 0x50));
static const uint16_t COL_GOOD   = PX(RGB565(0x4C, 0xC4, 0x7A));
static const uint16_t COL_FACE   = PX(RGB565(0x1A, 0x16, 0x22));

// Needs are shown as "how full is the bar", so hunger is displayed inverted: a full
// hunger bar would read as a good thing otherwise.
static const uint16_t COL_HUNGER = PX(RGB565(0xE8, 0x9B, 0x3C));
static const uint16_t COL_FUN    = PX(RGB565(0x5A, 0x9E, 0xE8));
static const uint16_t COL_CLEAN  = PX(RGB565(0x4C, 0xC4, 0x7A));
static const uint16_t COL_ENERGY = PX(RGB565(0xC8, 0x6B, 0xD8));

// ------------------------------------------------------------------ buttons ---
enum { ACT_FEED, ACT_SNACK, ACT_PLAY, ACT_WASH, ACT_CLEAN, ACT_MED, ACT_SLEEP, ACT_COUNT };

static const char *ACT_LABEL[ACT_COUNT] = { "FEED", "SNACK", "PLAY", "WASH",
                                            "CLEAN", "MED", "SLEEP" };
static const uint16_t ACT_COLOR[ACT_COUNT] = {
    PX(RGB565(0x4C, 0xC4, 0x7A)),   // feed
    PX(RGB565(0xC8, 0xA0, 0x4C)),   // snack
    PX(RGB565(0xE8, 0x6B, 0xA0)),   // play
    PX(RGB565(0x4C, 0x9E, 0xE8)),   // wash
    PX(RGB565(0x9C, 0x7A, 0x4C)),   // clean
    PX(RGB565(0xE0, 0x50, 0x50)),   // med
    PX(RGB565(0x8A, 0x6B, 0xD8)),   // sleep
};

// ------------------------------------------------------------------ helpers ---
static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void GamePet::setFlag(uint8_t f, bool on)
{
    if (on) S.flags |= f; else S.flags &= (uint8_t)~f;
}

void GamePet::say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    msgAgeMs = 0;
}

int GamePet::stageForAge() const
{
    const float m = ageMin();
    if (m < HATCH_MIN) return 0;
    if (m < TEEN_MIN)  return 1;
    if (m < ADULT_MIN) return 2;
    return 3;
}

float GamePet::careScore() const
{
    return clampf((100.0f - S.hunger) / 100.0f * 0.35f +
                  (100.0f - S.fun)    / 100.0f * 0.30f +
                   S.clean            / 100.0f * 0.20f +
                   S.energy           / 100.0f * 0.15f, 0.0f, 1.0f);
}

const pet_sprite_t &GamePet::sprite() const
{
    // Stage picks the art; being asleep swaps in the closed-eye variant.
    int idx = (S.stage >= 3) ? 5 : (S.stage == 2 ? 3 : 1);
    if (has(PF_SLEEPING)) idx += 1;
    return pet_sprites[idx];
}

// -------------------------------------------------------------------- NVS -----
void GamePet::load()
{
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) {
        // The magic has to travel inside the same blob as the state. Writing it as a
        // separate putBytes first does not work: the second write replaces the value
        // rather than appending to it, so the tag is gone by the time it is read back
        // and every load looks like "no save found".
        PetSave blob;
        const size_t got = prefs.isKey("blob")
                               ? prefs.getBytes("blob", &blob, sizeof(blob))
                               : 0;
        if (got == sizeof(blob) && blob.magic == SAVE_MAGIC) {
            S = blob.state;
            prefs.end();
            // Sanity: a corrupt or half-written blob should not resurrect a pet with
            // nonsense needs.
            if (S.stage > 3 || S.hunger > 200.0f || S.clean > 200.0f ||
                S.fun > 200.0f || S.energy > 200.0f) {
                Serial.println("[pet] save looked corrupt; starting a new egg");
                resetToNewEgg();
            } else {
                Serial.printf("[pet] restored: stage=%u ageMin=%.1f\n", S.stage, ageMin());
                return;
            }
        } else {
            prefs.end();
        }
    }
    resetToNewEgg();
    Serial.println("[pet] no save found; a new egg was laid");
}

void GamePet::save()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.println("[pet] could not open nvs for writing");
        return;
    }
    PetSave blob;
    blob.magic = SAVE_MAGIC;
    blob.state = S;
    prefs.putBytes("blob", &blob, sizeof(blob));
    prefs.end();
}

void GamePet::resetToNewEgg()
{
    memset(&S, 0, sizeof(S));
    S.sessionId  = petSessionId;
    S.lastSeenMs = millis();
    S.hunger = 35.0f;
    S.fun    = 40.0f;
    S.clean  = 75.0f;
    S.energy = 90.0f;
    S.stage  = 0;
    setFlag(PF_POOP, false);
    setFlag(PF_SLEEPING, false);
    setFlag(PF_SICK, false);
    setFlag(PF_RUNAWAY, false);
    say("clawd laid an egg - %d min", (int)HATCH_MIN);
    save();
    Serial.println("[pet] new egg laid (no valid save)");
}

// -------------------------------------------------------------- simulation ----
void GamePet::decay(uint32_t sec)
{
    // The egg does not age internally, and a pet that has left is not simulated.
    if (S.stage == 0 || has(PF_RUNAWAY)) return;

    const float mins = sec / 60.0f;
    const float sickMult  = has(PF_SICK)     ? SICK_MULT  : 1.0f;
    const float sleepMult = has(PF_SLEEPING) ? SLEEP_MULT : 1.0f;

    S.hunger = clampf(S.hunger + R_HUNGER * mins * sickMult * sleepMult, 0.0f, 100.0f);
    S.fun    = clampf(S.fun    + R_FUN    * mins * sickMult * sleepMult, 0.0f, 100.0f);
    S.clean  = clampf(S.clean  - R_CLEAN  * mins * sickMult * sleepMult, 0.0f, 100.0f);
    if (has(PF_POOP)) {
        S.clean = clampf(S.clean - R_POOP * mins * sickMult * sleepMult, 0.0f, 100.0f);
    }

    if (has(PF_SLEEPING)) {
        S.energy = clampf(S.energy + R_SLEEP_ENERGY * mins, 0.0f, 100.0f);
        if (S.energy >= 100.0f) {
            setFlag(PF_SLEEPING, false);
            say("clawd slept it off");
        }
    } else {
        S.energy = clampf(S.energy - R_ENERGY * mins * sickMult, 0.0f, 100.0f);
    }
}

void GamePet::checkGrow()
{
    if (S.stage == 0) {
        if (ageMin() >= HATCH_MIN) {
            S.stage = 1;
            say("the egg cracked - clawd hatched!");
            save();
        }
        return;
    }
    const int target = stageForAge();
    if (target > S.stage) {
        // Poor care delays growing up, except that adulthood is only a matter of
        // time -- the original is explicit about that softening.
        if (careScore() >= 0.4f || target >= 3) {
            S.stage = (uint8_t)target;
            say("clawd grew into a %s", STAGE_NAMES[target]);
            save();   // a milestone is worth an immediate write
        }
    }
}

void GamePet::checkSick()
{
    if (has(PF_SICK)) return;
    int crits = 0;
    if (S.hunger >= 85.0f) crits++;
    if (S.fun    >= 85.0f) crits++;
    if (S.clean  <= 15.0f) crits++;
    if (S.energy <=  8.0f) crits++;
    if (crits >= 2) {
        setFlag(PF_SICK, true);
        say("clawd is sick - give it medicine");
    }
}

void GamePet::doRunaway(const char *why)
{
    setFlag(PF_RUNAWAY, true);
    setFlag(PF_SLEEPING, false);
    say("%s", why);
    save();
}

void GamePet::checkRunaway()
{
    if (has(PF_RUNAWAY)) return;

    if (S.hunger >= 100.0f) {
        if (S.hungerPeakSec == 0) S.hungerPeakSec = S.ageSec;
        else if (S.ageSec - S.hungerPeakSec > RUNAWAY_HUNGER_SEC) {
            doRunaway("starved - clawd ran away");
            return;
        }
    } else {
        S.hungerPeakSec = 0;
    }

    int crits = 0;
    if (S.hunger >= 85.0f) crits++;
    if (S.fun    >= 85.0f) crits++;
    if (S.clean  <= 10.0f) crits++;

    if (crits >= 3) {
        if (S.criticalSinceSec == 0) S.criticalSinceSec = S.ageSec;
        else if (S.ageSec - S.criticalSinceSec > RUNAWAY_CRIT_SEC) {
            doRunaway("ignored for too long - clawd ran away");
        }
    } else {
        S.criticalSinceSec = 0;
    }
}

void GamePet::checkPoop(uint32_t sec)
{
    if (has(PF_POOP) || has(PF_SLEEPING) || S.stage == 0) return;
    if (S.hunger <= 45.0f) return;
    const float chance = (sec / 60.0f) * POOP_CHANCE_PER_MIN;
    if ((random(0, 10000) / 10000.0f) < chance) {
        setFlag(PF_POOP, true);
        say("clawd made a mess - clean it up");
    }
}

void GamePet::tick(uint32_t sec)
{
    S.ageSec += sec;
    decay(sec);
    checkPoop(sec);
    checkSick();
    checkRunaway();
    if (!has(PF_RUNAWAY)) checkGrow();
}

// ------------------------------------------------------------------ actions ---
void GamePet::act(int a)
{
    if (has(PF_RUNAWAY)) {
        if (a == ACT_FEED) {          // "re-adopt": any action starts a fresh egg
            resetToNewEgg();
        }
        return;
    }

    if (S.stage == 0) {
        const int left = (int)ceilf(HATCH_MIN - ageMin());
        say("still an egg - %d min to go", left > 0 ? left : 0);
        return;
    }

    // The original only allows tending a sleeping pet with sleep/clean/med.
    if (has(PF_SLEEPING) && a != ACT_SLEEP && a != ACT_CLEAN && a != ACT_MED) {
        say("clawd is asleep - let it rest");
        return;
    }

    switch (a) {
    case ACT_FEED:
        S.hunger = clampf(S.hunger - 22.0f, 0, 100);
        S.fun    = clampf(S.fun    +  4.0f, 0, 100);
        S.energy = clampf(S.energy +  3.0f, 0, 100);
        if (!has(PF_POOP) && (random(0, 100) < 12)) { setFlag(PF_POOP, true); say("ate well, then made a mess"); }
        else say("clawd ate happily");
        break;
    case ACT_SNACK:
        S.hunger = clampf(S.hunger -  8.0f, 0, 100);
        S.fun    = clampf(S.fun    +  8.0f, 0, 100);
        if (!has(PF_POOP) && (random(0, 100) < 18)) { setFlag(PF_POOP, true); say("too many snacks - a mess"); }
        else say("the snack perked clawd up");
        break;
    case ACT_PLAY:
        S.fun    = clampf(S.fun    - 25.0f, 0, 100);
        S.hunger = clampf(S.hunger + 10.0f, 0, 100);
        S.energy = clampf(S.energy - 12.0f, 0, 100);
        say("clawd played until it puffed");
        break;
    case ACT_WASH:
        S.clean  = clampf(S.clean  + 35.0f, 0, 100);
        S.energy = clampf(S.energy -  6.0f, 0, 100);
        say("clawd is squeaky clean");
        break;
    case ACT_CLEAN:
        if (!has(PF_POOP)) return;
        setFlag(PF_POOP, false);
        S.clean = clampf(S.clean + 10.0f, 0, 100);
        S.fun   = clampf(S.fun   -  5.0f, 0, 100);
        say("cleaned up");
        break;
    case ACT_MED:
        if (!has(PF_SICK)) return;
        setFlag(PF_SICK, false);
        S.hunger = clampf(S.hunger - 12.0f, 0, 100);
        S.energy = clampf(S.energy -  8.0f, 0, 100);
        say("the medicine helped");
        break;
    case ACT_SLEEP:
        setFlag(PF_SLEEPING, !has(PF_SLEEPING));
        say(has(PF_SLEEPING) ? "clawd fell asleep" : "clawd woke up");
        break;
    }

    checkSick();
    save();
}

// ------------------------------------------------------------------ drawing ---
void GamePet::drawIcon(float cx, float cy, float size) const
{
    // The baby sprite, scaled up: recognisably the same creature as the rolling
    // game's, drawn from its own art set.
    const pet_sprite_t &s = pet_sprites[1];
    const float scale = size / (float)s.h;
    const float ox = cx - s.w * scale * 0.5f;
    const float oy = cy - s.h * scale * 0.5f;
    for (int y = 0; y < s.h; y++) {
        for (int x = 0; x < s.w; x++) {
            const uint8_t idx = (s.bits[y * s.rowBytes + (x >> 1)] >> ((x & 1) * 4)) & 0x0F;
            if (!idx) continue;
            gfx::fillRect((int)(ox + x * scale), (int)(oy + y * scale),
                          (int)ceilf(scale), (int)ceilf(scale), PX(pet_palette[idx]));
        }
    }
}

void GamePet::drawPetSprite()
{
    const pet_sprite_t &s = sprite();
    // A gentle bob, matching the CSS animation in the original.
    const float bob = has(PF_SLEEPING) ? 0.0f : sinf(bobPhase) * 2.5f;
    const float scale = PET_SCALE;
    const float ox = PET_CX - s.w * scale * 0.5f;
    const float oy = PET_CY - s.h * scale * 0.5f + bob;

    for (int y = 0; y < s.h; y++) {
        for (int x = 0; x < s.w; x++) {
            const uint8_t idx = (s.bits[y * s.rowBytes + (x >> 1)] >> ((x & 1) * 4)) & 0x0F;
            if (!idx) continue;
            gfx::fillRect((int)lroundf(ox + x * scale), (int)lroundf(oy + y * scale),
                          (int)scale, (int)scale, PX(pet_palette[idx]));
        }
    }
}

void GamePet::drawBars()
{
    struct { float v; uint16_t col; bool invert; } rows[4] = {
        { S.hunger, COL_HUNGER, true  },
        { S.fun,    COL_FUN,    true  },
        { S.clean,  COL_CLEAN,  false },
        { S.energy, COL_ENERGY, false },
    };

    for (int i = 0; i < 4; i++) {
        const int y = BAR_Y0 + i * BAR_PITCH;
        // Filled portion reads as "how much of this need is satisfied", so hunger
        // and boredom are drawn inverted -- a nearly empty bar there means the pet
        // is fine.
        const float frac = (rows[i].invert ? (100.0f - rows[i].v) : rows[i].v) / 100.0f;
        gfx::fillRect(BAR_X - 12, y, 8, 7, gfx::dim(rows[i].col, 0.4f));
        gfx::fillRect(BAR_X, y, BAR_W, BAR_H, COL_TRACK);
        gfx::fillRect(BAR_X, y, (int)(BAR_W * frac + 0.5f), BAR_H, rows[i].col);
    }
}

void GamePet::layoutButtons(float *xs, float *ys, float *radii, bool *enabled, int *count) const
{
    int n = 0;
    const int row1[4] = { ACT_FEED, ACT_SNACK, ACT_PLAY, ACT_WASH };
    const int row2[3] = { ACT_CLEAN, ACT_MED, ACT_SLEEP };

    for (int i = 0; i < 4; i++) {
        xs[n] = PET_CX + (i - 1.5f) * BTN_PITCH;
        ys[n] = BTN_ROW1_Y;
        radii[n] = BTN_R;
        enabled[n] = true;
        n++;
    }
    for (int i = 0; i < 3; i++) {
        xs[n] = PET_CX + (i - 1.0f) * BTN_PITCH;
        ys[n] = BTN_ROW2_Y;
        radii[n] = BTN_R;
        // Clean and medicine are only meaningful when there is something to do, but
        // they stay on screen dimmed so their position never moves under the finger.
        enabled[n] = (row2[i] == ACT_CLEAN) ? has(PF_POOP)
                   : (row2[i] == ACT_MED)   ? has(PF_SICK)
                   : true;
        n++;
    }
    *count = n;
}

void GamePet::drawButtons()
{
    float xs[ACT_COUNT], ys[ACT_COUNT], rs[ACT_COUNT];
    bool en[ACT_COUNT];
    int n = 0;
    layoutButtons(xs, ys, rs, en, &n);

    for (int i = 0; i < n; i++) {
        const bool sel = (i == lastTapped);
        uint16_t c = ACT_COLOR[i];
        if (!en[i]) c = gfx::dim(c, 0.72f);
        if (sel)    c = gfx::mix(c, PX(RGB565(0xFF, 0xFF, 0xFF)), 0.45f);

        gfx::fillCircle(xs[i], ys[i], rs[i], gfx::dim(c, 0.78f));
        gfx::strokeCircle(xs[i], ys[i], rs[i], 2.0f, c);

        const char *lab = ACT_LABEL[i];
        const int w = gfx::textWidth(lab, 1);
        gfx::text(lab, (int)(xs[i] - w * 0.5f), (int)(ys[i] - 3), en[i] ? COL_TEXT : COL_DIM, 1);
    }
}

// -------------------------------------------------------------------- frame ---
bool GamePet::selfTest()
{
    // Load first. Running this before anything has been read means "the state to
    // put back afterwards" is an empty struct, and the test would overwrite the
    // real save with zeros on its way out.
    load();
    const State before = S;

    State probe = S;
    probe.stage  = 2;
    probe.ageSec = 1234;
    probe.hunger = 55.5f;

    S = probe;
    save();

    // Wipe RAM so load() has to read the flash rather than find the values already
    // sitting in the struct.
    memset(&S, 0, sizeof(S));
    load();

    const bool ok = (S.stage == 2) && (S.ageSec == 1234) && (fabsf(S.hunger - 55.5f) < 0.01f);
    Serial.printf("[pet] nvs round-trip: %s (stage=%u age=%u hunger=%.1f)\n",
                  ok ? "PASS" : "FAIL", (unsigned)S.stage, (unsigned)S.ageSec, S.hunger);

    S = before;      // leave the real pet as it was
    save();
    return ok;
}

void GamePet::enter()
{
    if (!petSessionId) petSessionId = (uint32_t)esp_random();

    // Work out how long the pet was off-screen *before* load() overwrites the
    // saved clock. Only valid within the same boot; after a power cycle millis()
    // has restarted and the gap is unknowable, so the pet simply waits.
    const uint32_t nowMs = millis();
    uint32_t awaySec = 0;

    load();

    if (S.sessionId == petSessionId && nowMs > S.lastSeenMs) {
        const uint32_t awayMs = nowMs - S.lastSeenMs;
        awaySec = awayMs / 1000;
        if (awaySec > AWAY_CAP_SEC) awaySec = AWAY_CAP_SEC;
    }
    if (awaySec > 0) {
        Serial.printf("[pet] %u s passed off-screen; applying\n", (unsigned)awaySec);
        tick(awaySec);          // ages it too, so a hatch can happen while away
        save();
    }
    S.sessionId  = petSessionId;
    S.lastSeenMs = nowMs;
    bobPhase = 0.0f;
    saveCounter = 0.0f;
    tickAccum = 0.0f;
    lastTapped = -1;
    dirtyY0 = 0;
    dirtyY1 = SCR_H - 1;
}

void GamePet::leave()
{
    save();   // never lose progress just because the user wandered off
    gfx::clear(COL_BG);
}

void GamePet::update(float dt)
{
    const uint32_t ms = (uint32_t)(dt * 1000.0f + 0.5f);
    S.lastSeenMs = millis();     // so time spent off-screen can be measured later

    // Advance the simulation in whole seconds. Taking dt/1000 directly would round
    // every 16 ms frame down to zero and the pet would never age.
    tickAccum += dt;
    const uint32_t wholeSec = (uint32_t)tickAccum;
    if (wholeSec > 0) {
        tickAccum -= (float)wholeSec;
        tick(wholeSec);
    }

    bobPhase += dt * 2.2f;
    if (bobPhase > 2.0f * PI_F) bobPhase -= 2.0f * PI_F;

    msgAgeMs += ms;

    // --- touch ---------------------------------------------------------------
    lastTapped = -1;
    if (input::tapped()) {
        float xs[ACT_COUNT], ys[ACT_COUNT], rs[ACT_COUNT];
        bool en[ACT_COUNT];
        int n = 0;
        layoutButtons(xs, ys, rs, en, &n);
        for (int i = 0; i < n; i++) {
            const float dx = input::tapX() - xs[i];
            const float dy = input::tapY() - ys[i];
            const float hit = rs[i] + 6.0f;    // a little forgiveness for fingers
            if (dx * dx + dy * dy <= hit * hit) {
                if (en[i]) {
                    act(i);
                    lastTapped = i;
                } else if (has(PF_RUNAWAY)) {
                    resetToNewEgg();           // the "adopt again" path
                }
                break;
            }
        }
        if (has(PF_RUNAWAY) && lastTapped < 0) resetToNewEgg();
    }

    saveCounter += ms;
    if (saveCounter >= SAVE_PERIOD_MS) {
        saveCounter = 0.0f;
        save();
    }
}

void GamePet::render()
{
    gfx::clear(COL_BG);
    for (int y = 22; y < SCR_H; y += 45) {
        for (int x = 22; x < SCR_W; x += 45) {
            gfx::fillRect(x, y, 2, 2, COL_DOT);
        }
    }

    if (has(PF_RUNAWAY)) {
        gfx::fillCircle(PET_CX, 168.0f, 96.0f, PX(RGB565(0x12, 0x12, 0x1E)));
        gfx::strokeCircle(PET_CX, 168.0f, 96.0f, 2.0f, gfx::dim(COL_WARN, 0.4f));
        gfx::textCentered("CLAWD RAN AWAY", 130, COL_WARN, 2);
        gfx::textCentered("it waited as long as it could", 160, COL_DIM, 1);
        gfx::textCentered("TAP TO ADOPT A NEW EGG", 196, COL_TEXT, 1);
        setDirty(0, SCR_H - 1);
        return;
    }

    drawBars();

    char head[32];
    const float m = ageMin();
    snprintf(head, sizeof(head), "%s  %02d:%02d", STAGE_NAMES[S.stage],
             (int)(m / 60.0f) % 100, (int)m % 60);
    gfx::textCentered(head, 92, COL_DIM, 1);

    drawPetSprite();

    // Status glyphs. Placed clear of the sprite rather than on top of it: the adult
    // art is 20*PET_SCALE wide, so anything nearer the centre disappears against the
    // body and reads as part of the pet.
    if (has(PF_POOP)) {
        gfx::fillRect((int)PET_CX + 74, (int)PET_CY + 30, 13, 9, PX(RGB565(0x9C, 0x7A, 0x4C)));
    }
    if (has(PF_SICK)) {
        gfx::fillCircle(PET_CX - 82.0f, PET_CY + 33.0f, 7.0f, COL_WARN);
    }

    if (msg[0] && msgAgeMs < MSG_LINGER_MS) {
        gfx::textCentered(msg, MSG_Y, COL_TEXT, 1);
    }

    drawButtons();
    setDirty(0, SCR_H - 1);
}
