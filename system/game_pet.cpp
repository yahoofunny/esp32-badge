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
//
// One deliberate departure: there is no egg stage. The original starts with a
// three-minute wait before anything can be done, which reads as a dead screen on a
// badge -- and the wait only ever happens once, so it is not really a mechanic.
// A new pet is born directly at the baby stage.
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
// Everything here is checked against the round bezel: the usable width at row y is
// 2*sqrt(R^2 - (y-cy)^2), R = 180, which is only about 274 px at the height of the
// heart meters and 229 px at the icon row.
// A terminal layout: a header line, a rule, two labelled meter rows, the creature,
// the last message, another rule, and a text menu of actions.
static const int HEAD_Y  = 44;
static const int RULE1_Y = 60;
static const int RULE2_Y = 272;
static const int RULE_X0 = 62, RULE_X1 = 298;

static const int HEART_TOTAL = 4;
static const int HEART_PITCH = 13;

static const int METER_LABEL_RIGHT = 168;   // labels are right-aligned to here
static const int METER_HEART_X     = 180;   // so the hearts line up
static const int METER_Y0          = 76;
static const int METER_PITCH       = 18;

static const float PET_CX = 180.0f, PET_CY = 176.0f;

static const int MSG_Y = 250;

static const int MENU_Y0    = 288;
static const int MENU_PITCH = 18;

// ------------------------------------------------------------------ colours ---
// The dark theme plus the pet's own palette.
//
// A monochrome-LCD imitation was tried here and dropped. It was faithful to the
// original hardware, but it threw away the artwork's colours -- which is most of what
// makes the creature appealing -- on a display perfectly capable of showing them.
// What is kept from the original is the structure: two heart meters, life stages, and
// care actions, rather than the 1996 palette.
static const uint16_t COL_BG          = PX(RGB565(0x0E, 0x0E, 0x16));
static const uint16_t COL_TEXT        = PX(RGB565(0xEC, 0xEC, 0xF4));
static const uint16_t COL_DIM         = PX(RGB565(0x6E, 0x6E, 0x80));
static const uint16_t COL_RULE        = PX(RGB565(0x2A, 0x2A, 0x3A));
static const uint16_t COL_HEART_FULL  = PX(RGB565(0xFF, 0x5A, 0x7A));
static const uint16_t COL_HEART_EMPTY = PX(RGB565(0x33, 0x33, 0x42));
// The same rust the creature is drawn in, and the same one Claude Code uses. It is
// the single accent for anything that is a label rather than a value.
static const uint16_t COL_ACCENT      = PX(RGB565(0xD9, 0x77, 0x57));

// ------------------------------------------------------------------ buttons ---
enum { ACT_FEED, ACT_SNACK, ACT_PLAY, ACT_WASH, ACT_CLEAN, ACT_MED, ACT_SLEEP, ACT_COUNT };

static const char *ACT_LABEL[ACT_COUNT] = { "FEED", "SNACK", "PLAY", "WASH",
                                            "CLEAN", "MED", "SLEEP" };
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
                Serial.println("[pet] save looked corrupt; starting a new clawd");
                resetToNewPet();
            } else {
                // Saves written before the egg stage was removed carry stage 0. Promote them
                // silently rather than letting checkGrow() announce that an egg grew
                // into a baby.
                if (S.stage == 0) S.stage = 1;
                Serial.printf("[pet] restored: stage=%u age=%.1f min hunger=%.0f\n",
                              S.stage, ageMin(), S.hunger);
                return;
            }
        } else {
            prefs.end();
        }
    }
    resetToNewPet();
    Serial.println("[pet] no save found; a new clawd was born");
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

void GamePet::resetToNewPet()
{
    memset(&S, 0, sizeof(S));
    S.sessionId  = petSessionId;
    S.lastSeenMs = millis();
    S.hunger = 35.0f;
    S.fun    = 40.0f;
    S.clean  = 75.0f;
    S.energy = 90.0f;
    S.stage  = 1;
    setFlag(PF_POOP, false);
    setFlag(PF_SLEEPING, false);
    setFlag(PF_SICK, false);
    setFlag(PF_RUNAWAY, false);
    say("clawd was born");
    save();
    Serial.println("[pet] a new clawd was born (no valid save)");
}

// -------------------------------------------------------------- simulation ----
void GamePet::decay(uint32_t sec)
{
    // A pet that has left is not simulated.
    if (has(PF_RUNAWAY)) return;          // stage 0 no longer occurs; see the note above

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
    if (has(PF_POOP) || has(PF_SLEEPING)) return;
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
        if (a == ACT_FEED) {          // "re-adopt": any action starts a fresh pet
            resetToNewPet();
        }
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
    // The baby sprite in LCD tones, so the launcher tile matches what the app itself
    // looks like: one ink colour with the eyes punched out.
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

int GamePet::stageScale() const
{
    // The art grows only slightly between stages (16x14, 16x16, 20x18), so at a fixed
    // scale a baby and an adult look like the same creature. Size carries the sense
    // of growth instead: about 64, 80 and 108 px tall.
    if (S.stage >= 3) return 6;
    if (S.stage == 2) return 5;
    return 4;
}

void GamePet::drawPetSprite()
{
    const pet_sprite_t &s = sprite();
    // A gentle bob, matching the CSS animation in the original.
    const float bob = has(PF_SLEEPING) ? 0.0f : sinf(bobPhase) * 2.5f;
    const float scale = (float)stageScale();
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

void GamePet::drawHeart(int cx, int cy, bool filled)
{
    const uint16_t c = filled ? COL_HEART_FULL : COL_HEART_EMPTY;
    gfx::fillCircle(cx - 2.0f, cy - 1.0f, 2.2f, c);
    gfx::fillCircle(cx + 2.0f, cy - 1.0f, 2.2f, c);
    for (int i = 0; i < 5; i++) {
        gfx::fillRect(cx - 3 + i, cy + 1 + i, 7 - 2 * i, 1, c);
    }
}

void GamePet::drawHearts(int x, int y, int filled, int total)
{
    for (int i = 0; i < total; i++) {
        drawHeart(x + i * HEART_PITCH, y, i < filled);
    }
}

void GamePet::drawHeartMeters()
{
    // Two meters of four, the original's read-out for hunger and happiness, with the
    // labels spelled out to the left. Four hearts is what the original shows; naming
    // them is borrowed from the terminal idiom the rest of the screen uses.
    const int hungerHearts = (int)lroundf((100.0f - S.hunger) / (100.0f / HEART_TOTAL));
    const int funHearts    = (int)lroundf((100.0f - S.fun)    / (100.0f / HEART_TOTAL));

    const char *names[2] = { "HUNGER", "HAPPY" };
    const int vals[2] = { hungerHearts, funHearts };
    for (int i = 0; i < 2; i++) {
        const int y = METER_Y0 + i * METER_PITCH;
        const int w = gfx::textWidth(names[i], 1);
        gfx::text(names[i], METER_LABEL_RIGHT - w, y, COL_DIM, 1);
        drawHearts(METER_HEART_X, y + 3, vals[i], HEART_TOTAL);
    }
}

void GamePet::drawRule(int y)
{
    gfx::fillRect(RULE_X0, y, RULE_X1 - RULE_X0, 1, COL_RULE);
}

// The action pictograms. Solid shapes only: a tile is drawn either as ink on the
// background or inverted as background on ink, and any interior detail would need a
// third colour to survive both.
static void glyph_bowl(int cx, int cy, uint16_t c)
{
    gfx::fillRect(cx - 5, cy - 1, 10, 1, c);
    for (int i = 0; i < 9; i++) gfx::fillRect(cx - 4 + i, cy + abs(i - 4) / 2, 1, 3, c);
}
static void glyph_candy(int cx, int cy, uint16_t c)
{
    gfx::fillCircle(cx, cy, 3.5f, c);
    gfx::fillRect(cx - 6, cy - 1, 3, 2, c);
    gfx::fillRect(cx + 4, cy - 1, 3, 2, c);
}
static void glyph_ball(int cx, int cy, uint16_t c)
{
    gfx::fillCircle(cx, cy, 4.5f, c);
}
static void glyph_drop(int cx, int cy, uint16_t c)
{
    for (int i = 0; i < 8; i++) gfx::fillRect(cx - i / 2, cy - 4 + i, 1 + i, 1, c);
}
static void glyph_broom(int cx, int cy, uint16_t c)
{
    gfx::fillRect(cx, cy - 6, 1, 6, c);
    for (int i = 0; i < 5; i++) gfx::fillRect(cx - 3 + i, cy + i / 2, 7 - i, 1, c);
}
static void glyph_cross(int cx, int cy, uint16_t c)
{
    gfx::fillRect(cx - 1, cy - 4, 3, 9, c);
    gfx::fillRect(cx - 4, cy - 1, 9, 3, c);
}
static void glyph_zzz(int cx, int cy, uint16_t c)
{
    gfx::fillRect(cx - 4, cy - 3, 7, 1, c);
    gfx::fillRect(cx + 2, cy - 2, 1, 1, c);
    gfx::fillRect(cx - 3, cy - 1, 7, 1, c);
    gfx::fillRect(cx - 3, cy + 2, 7, 1, c);
}

static void (*const ACT_GLYPH[ACT_COUNT])(int, int, uint16_t) = {
    glyph_bowl, glyph_candy, glyph_ball, glyph_drop, glyph_broom, glyph_cross, glyph_zzz,
};

// The actions are a text menu, two rows of words, with the last one used highlighted.
// A row of pictograms was tried first and abandoned: seven hand-drawn glyphs at 22 px
// are guesswork, and nothing about a bowl or a broom tells you it means feed or clean.
// In a terminal idiom the names are simply spelled out.
void GamePet::layoutMenu(float *xs, float *ys, float *ws, bool *enabled, int *count) const
{
    const int rowSplit = 4;                        // four on the first row, three after
    const float gap = 14.0f;

    for (int row = 0; row < 2; row++) {
        const int first = (row == 0) ? 0 : rowSplit;
        const int last  = (row == 0) ? rowSplit : ACT_COUNT;

        float total = 0.0f;
        for (int i = first; i < last; i++) {
            total += gfx::textWidth(ACT_LABEL[i], 1) + gap;
        }
        total -= gap;

        float x = SCR_CX - total * 0.5f;
        const float y = MENU_Y0 + row * MENU_PITCH;
        for (int i = first; i < last; i++) {
            xs[i] = x;
            ys[i] = y;
            ws[i] = (float)gfx::textWidth(ACT_LABEL[i], 1);
            // Clean and medicine only mean something when there is something to do,
            // but they stay listed so no word ever moves under the finger.
            enabled[i] = (i == ACT_CLEAN) ? has(PF_POOP)
                       : (i == ACT_MED)   ? has(PF_SICK)
                       : true;
            x += ws[i] + gap;
        }
    }
    *count = ACT_COUNT;
}

void GamePet::drawMenu()
{
    float xs[ACT_COUNT], ys[ACT_COUNT], ws[ACT_COUNT];
    bool en[ACT_COUNT];
    int n = 0;
    layoutMenu(xs, ys, ws, en, &n);

    for (int i = 0; i < n; i++) {
        if (i == lastTapped) {
            gfx::fillRect((int)xs[i] - 4, (int)ys[i] - 3, (int)ws[i] + 8, 14,
                          gfx::dim(COL_ACCENT, 0.65f));
        }
        gfx::text(ACT_LABEL[i], (int)xs[i], (int)ys[i],
                  en[i] ? (i == lastTapped ? COL_TEXT : COL_ACCENT) : COL_DIM, 1);
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
        float xs[ACT_COUNT], ys[ACT_COUNT], ws[ACT_COUNT];
        bool en[ACT_COUNT];
        int n = 0;
        layoutMenu(xs, ys, ws, en, &n);
        for (int i = 0; i < n; i++) {
            // Generous vertically and a few pixels wide, because these are words
            // rather than tiles and a finger is not a pointer.
            const float dx = input::tapX() - (xs[i] + ws[i] * 0.5f);
            const float dy = input::tapY() - (ys[i] + 3);
            if (fabsf(dx) <= ws[i] * 0.5f + 6.0f && fabsf(dy) <= 12.0f) {
                if (en[i]) {
                    act(i);
                    lastTapped = i;
                } else if (has(PF_RUNAWAY)) {
                    resetToNewPet();           // the "adopt again" path
                }
                break;
            }
        }
        if (has(PF_RUNAWAY) && lastTapped < 0) resetToNewPet();
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

    if (has(PF_RUNAWAY)) {
        gfx::textCentered("CLAWD RAN AWAY", 130, COL_ACCENT, 2);
        gfx::textCentered("it waited as long as it could", 162, COL_DIM, 1);
        gfx::textCentered("TAP TO ADOPT A NEW PET", 200, COL_TEXT, 1);
        setDirty(0, SCR_H - 1);
        return;
    }

    // Header: what it is, how old it is, on one line with the accent.
    char head[40];
    const float m = ageMin();
    snprintf(head, sizeof(head), "CLAWD PET  |  %s  |  %02d:%02d", STAGE_NAMES[S.stage],
             (int)(m / 60.0f) % 100, (int)m % 60);
    gfx::textCentered(head, HEAD_Y, COL_ACCENT, 1);
    drawRule(RULE1_Y);

    drawHeartMeters();
    drawPetSprite();

    // Status glyphs, kept clear of the sprite: at the adult scale the body reaches
    // about PET_CX +/- 60, so anything nearer the middle disappears into it.
    // Status markers as words, for the same reason the menu is words.
    if (has(PF_POOP)) {
        gfx::text("MESS", (int)PET_CX + 66, (int)PET_CY + 30, COL_ACCENT, 1);
    }
    if (has(PF_SICK)) {
        gfx::text("SICK", (int)PET_CX - 96, (int)PET_CY + 30, COL_HEART_FULL, 1);
    }

    if (msg[0] && msgAgeMs < MSG_LINGER_MS) {
        gfx::textCentered(msg, MSG_Y, COL_TEXT, 1);
    }

    drawRule(RULE2_Y);
    drawMenu();
    setDirty(0, SCR_H - 1);
}
