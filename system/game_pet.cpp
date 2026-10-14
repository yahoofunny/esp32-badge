#include "game_pet.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Display_ST77916.h"
#include "esp_random.h"
#include "gfx.h"
#include "input.h"

// --------------------------------------------------------------- the numbers ---
// Decay is quoted per minute, as in the original's config. These are faster than a
// real Tamagotchi's because a badge is held for minutes at a time, not lived with: a
// bar left alone empties in roughly an hour and a half.
static const float RATE_HUNGER      = 1.2f;
static const float RATE_HAPPINESS   = 0.9f;
static const float RATE_ENERGY      = 0.7f;
static const float RATE_CLEANLINESS = 0.6f;
static const float SLEEP_ENERGY_PER_MIN = 2.0f;
static const float SLEEP_HUNGER_SCALE   = 0.5f;

// Health is not maintained directly; it drags towards the average of the four needs.
static const float HEALTH_DRAG_PER_MIN = 0.10f;

// Sickness begins and ends on its own, from health and cleanliness.
static const float SICK_AT_HEALTH      = 30.0f;
static const float SICK_AT_CLEANLINESS = 20.0f;
static const float WELL_AT_HEALTH      = 60.0f;
static const float WELL_AT_CLEANLINESS = 50.0f;

// Growth. Care only accrues while the pet is in good shape, so a pet that is merely
// old does not grow up.
static const float CARE_PER_MIN  = 1.0f;
static const float CARE_GOOD_AVG = 70.0f;
struct StageGate { uint32_t ageSec; float care; };
static const StageGate GATES[3] = {
    {  15UL * 60,  20.0f },   // baby  -> child
    { 120UL * 60, 120.0f },   // child -> teen
    { 300UL * 60, 300.0f },   // teen  -> adult
};
static const char *const STAGE_NAMES[4] = { "BABY", "CHILD", "TEEN", "ADULT" };

static const uint32_t SAVE_PERIOD_MS   = 15000;
static const uint32_t THOUGHT_EVERY_MS = 40000;
static const uint32_t AWAY_CAP_SEC     = 4UL * 3600;

static const char *const NVS_NAMESPACE = "clawdpet";
static const uint32_t SAVE_MAGIC = 0x50544543;   // 'CETP'

static uint32_t petSessionId = 0;

// --------------------------------------------------------------- the face ------
// The original draws its face with kaomoji -- round-eyed bear faces and their many
// variants -- which are Unicode, and therefore unrenderable by the 8x8 ASCII font
// used here.
//
// Writing the equivalent in ASCII was tried first and does not work: at 8x8 the
// parenthesis is almost a straight stroke, so scaling it up produces square brackets
// and the result reads as [ ^ _ ^ ] rather than as a face. Rendering it on the host
// before flashing is what caught that, and it is why the face is drawn.
//
// Drawing is also what a kaomoji is: an eye style, a mouth style, and optionally a
// pair of raised arms.
enum { EYE_ROUND, EYE_HAPPY, EYE_SHUT, EYE_CROSS };
enum { MOUTH_SMILE, MOUTH_OPEN, MOUTH_FLAT, MOUTH_FROWN };

struct FaceDef { uint8_t eyes; uint8_t mouth; bool arms; };

static const FaceDef F_IDLE[]  = { {EYE_ROUND, MOUTH_SMILE, false},
                                   {EYE_SHUT,  MOUTH_FLAT,  false},
                                   {EYE_ROUND, MOUTH_SMILE, false},
                                   {EYE_ROUND, MOUTH_OPEN,  false},
                                   {EYE_ROUND, MOUTH_SMILE, false},
                                   {EYE_HAPPY, MOUTH_OPEN,  false} };
static const FaceDef F_HAPPY[] = { {EYE_HAPPY, MOUTH_SMILE, true},
                                   {EYE_HAPPY, MOUTH_SMILE, false},
                                   {EYE_HAPPY, MOUTH_OPEN,  true} };
static const FaceDef F_EAT[]   = { {EYE_ROUND, MOUTH_OPEN,  false},
                                   {EYE_SHUT,  MOUTH_OPEN,  false},
                                   {EYE_ROUND, MOUTH_OPEN,  false} };
static const FaceDef F_SLEEP[] = { {EYE_SHUT, MOUTH_FLAT,  false},
                                   {EYE_SHUT, MOUTH_FROWN, false} };
static const FaceDef F_BATH[]  = { {EYE_HAPPY, MOUTH_SMILE, false},
                                   {EYE_ROUND, MOUTH_OPEN,  false},
                                   {EYE_HAPPY, MOUTH_SMILE, false} };
static const FaceDef F_SICK[]  = { {EYE_CROSS, MOUTH_FROWN, false},
                                   {EYE_SHUT,  MOUTH_FROWN, false} };

struct AnimDef { const FaceDef *frames; uint8_t count; };
static const AnimDef ANIMS[ANIM_COUNT] = {
    { F_IDLE,  6 },
    { F_HAPPY, 3 },
    { F_EAT,   3 },
    { F_SLEEP, 2 },
    { F_BATH,  3 },
    { F_SICK,  2 },
};
static const float FRAME_SEC       = 0.45f;
static const float ACTION_HOLD_SEC = 2.6f;

// Face geometry, in pixels measured from the face centre.
static const int EYE_DX = 26, EYE_DY = -8, EYE_R = 6;
static const int MOUTH_DY = 14;

static void drawEye(int x, int y, int style, uint16_t c)
{
    switch (style) {
    case EYE_HAPPY:
        // An upward arc: the eye shut in a smile.
        for (int i = -EYE_R; i <= EYE_R; i++) {
            gfx::fillRect(x + i, y - (EYE_R - abs(i)) / 2, 1, 3, c);
        }
        break;
    case EYE_SHUT:
        gfx::fillRect(x - EYE_R, y, 2 * EYE_R + 1, 2, c);
        break;
    case EYE_CROSS:
        gfx::line((float)(x - EYE_R), (float)(y - EYE_R), (float)(x + EYE_R), (float)(y + EYE_R), c);
        gfx::line((float)(x + EYE_R), (float)(y - EYE_R), (float)(x - EYE_R), (float)(y + EYE_R), c);
        break;
    default:
        gfx::fillCircle((float)x, (float)y, (float)EYE_R, c);
        break;
    }
}

static void drawMouth(int x, int y, int style, uint16_t c)
{
    const int half = EYE_DX - 8;
    switch (style) {
    case MOUTH_SMILE:
        for (int i = -half; i <= half; i++) {
            gfx::fillRect(x + i, y + abs(i) / 3, 1, 2, c);
        }
        break;
    case MOUTH_OPEN:
        gfx::fillCircle((float)x, (float)y + 4.0f, 6.0f, c);
        break;
    case MOUTH_FROWN:
        for (int i = -half; i <= half; i++) {
            gfx::fillRect(x + i, y + 5 - abs(i) / 3, 1, 2, c);
        }
        break;
    default:
        gfx::fillRect(x - 6, y + 2, 13, 2, c);
        break;
    }
}

// ------------------------------------------------------------ what it says -----
// Grouped by what is wrong, checked in order of severity. The original has hundreds of
// these; a dozen or so is enough for the pet to feel like it has opinions.
static const char *const T_SICK[]   = { "i don't feel well", "everything aches", "medic... please" };
static const char *const T_HUNGRY[] = { "my stomach is rumbling", "got anything to eat?", "so hungry" };
static const char *const T_TIRED[]  = { "so sleepy...", "i could nap", "eyes are heavy" };
static const char *const T_DIRTY[]  = { "i need a bath", "feeling grubby", "i smell funny" };
static const char *const T_SAD[]    = { "entertain me?", "it is boring here", "play with me?" };
static const char *const T_GOOD[]   = { "life is good", "i like it here", "thanks for looking after me" };
static const char *const T_IDLE[]   = { "just hanging out", "nice day", "watching the pixels", "hello!" };

static const char *const NAMES[] = { "CLAWD", "PIP", "MOMO", "BOB", "ZIGGY", "NOVA" };
static const int NAME_COUNT = (int)(sizeof(NAMES) / sizeof(NAMES[0]));

// ------------------------------------------------------------------ layout -----
static const int HEAD_Y  = 44;
static const int RULE1_Y = 58;
static const int METER_COL_A = 54, METER_COL_B = 196;
static const int METER_ROW0  = 74,  METER_ROW1 = 88;
static const int HEALTH_Y    = 106;
static const int FACE_CY     = 178;
static const int THOUGHT_Y   = 240;
static const int RULE2_Y     = 262;
static const int MENU_Y0     = 278, MENU_PITCH = 18;
static const int RULE_X0 = 62, RULE_X1 = 298;
static const int BAR_CHARS = 8;       // '#' filled, '.' empty

static const uint16_t COL_BG     = PX(RGB565(0x0E, 0x0E, 0x16));
static const uint16_t COL_TEXT   = PX(RGB565(0xEC, 0xEC, 0xF4));
static const uint16_t COL_DIM    = PX(RGB565(0x6E, 0x6E, 0x80));
static const uint16_t COL_RULE   = PX(RGB565(0x2A, 0x2A, 0x3A));
static const uint16_t COL_ACCENT = PX(RGB565(0xD9, 0x77, 0x57));   // clawd's rust
static const uint16_t COL_WARN   = PX(RGB565(0xE0, 0x50, 0x50));

static const char *const ACT_LABEL[ACT_COUNT] = { "FEED", "PLAY", "PET", "SLEEP", "BATHE", "NAME" };

// ------------------------------------------------------------------ helpers ----
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
    vsnprintf(thought, sizeof(thought), fmt, ap);
    va_end(ap);
    thoughtAgeSec = 0.0f;
}

float GamePet::averageCare() const
{
    return (S.hunger + S.happiness + S.energy + S.cleanliness) / 4.0f;
}

void GamePet::setAnimation(int a, float holdSec)
{
    anim = (uint8_t)a;
    animFrame = 0;
    animFrameSec = 0.0f;
    animHoldSec = holdSec;
}

// -------------------------------------------------------------------- NVS -----
void GamePet::load()
{
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) {
        // The magic travels inside the same blob as the state: writing it as a separate
        // putBytes first does not work, because the following write replaces the value
        // rather than appending, and the tag is gone by the time it is read back.
        PetSave blob;
        const size_t got = prefs.isKey("blob") ? prefs.getBytes("blob", &blob, sizeof(blob)) : 0;
        if (got == sizeof(blob) && blob.magic == SAVE_MAGIC) {
            S = blob.state;
            prefs.end();
            const bool sane = S.stage <= 3 && S.name[0] != 0 &&
                              S.hunger <= 100.5f && S.happiness <= 100.5f &&
                              S.energy <= 100.5f && S.cleanliness <= 100.5f &&
                              S.health <= 100.5f;
            if (sane) {
                Serial.printf("[pet] restored: %s stage=%u age=%.1f min health=%.0f\n",
                              S.name, S.stage, S.ageSec / 60.0f, S.health);
                return;
            }
            Serial.println("[pet] save looked corrupt; starting a new pet");
        } else {
            prefs.end();
        }
    }
    resetToNewPet();
    Serial.println("[pet] no save found; a new pet was born");
}

void GamePet::save()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    PetSave blob;
    blob.magic = SAVE_MAGIC;
    blob.state = S;
    prefs.putBytes("blob", &blob, sizeof(blob));
    prefs.end();
}

void GamePet::resetToNewPet()
{
    memset(&S, 0, sizeof(S));
    S.sessionId   = petSessionId;
    S.lastSeenMs  = millis();
    S.hunger      = 70.0f;
    S.happiness   = 70.0f;
    S.energy      = 80.0f;
    S.cleanliness = 90.0f;
    S.health      = 80.0f;
    S.stage       = 0;
    strncpy(S.name, NAMES[random(0, NAME_COUNT)], sizeof(S.name) - 1);
    say("%s was born", S.name);
    save();
}

bool GamePet::selfTest()
{
    // Load first: running this before anything has been read means "the state to put
    // back afterwards" is an empty struct, and it would overwrite the real save.
    load();
    const State before = S;

    State probe = S;
    probe.stage = 2;
    probe.ageSec = 1234;
    probe.hunger = 55.5f;
    strncpy(probe.name, "PROBE", sizeof(probe.name) - 1);

    S = probe;
    save();
    memset(&S, 0, sizeof(S));      // force load() to read flash rather than stale RAM
    load();

    const bool ok = (S.stage == 2) && (S.ageSec == 1234) &&
                    (fabsf(S.hunger - 55.5f) < 0.01f) && (strcmp(S.name, "PROBE") == 0);
    Serial.printf("[pet] nvs round-trip: %s (stage=%u age=%u name=%s)\n",
                  ok ? "PASS" : "FAIL", (unsigned)S.stage, (unsigned)S.ageSec, S.name);

    S = before;
    save();
    return ok;
}

// -------------------------------------------------------------- simulation -----
void GamePet::decay(uint32_t sec)
{
    const float mins = sec / 60.0f;
    if (mins <= 0.0f) return;

    if (has(PF_ASLEEP)) {
        // Sleeping restores energy and slows appetite; nothing else moves.
        S.energy = clampf(S.energy + SLEEP_ENERGY_PER_MIN * mins, 0.0f, 100.0f);
        S.hunger = clampf(S.hunger - RATE_HUNGER * mins * SLEEP_HUNGER_SCALE, 0.0f, 100.0f);
        return;
    }

    S.hunger      = clampf(S.hunger      - RATE_HUNGER      * mins, 0.0f, 100.0f);
    S.happiness   = clampf(S.happiness   - RATE_HAPPINESS   * mins, 0.0f, 100.0f);
    S.energy      = clampf(S.energy      - RATE_ENERGY      * mins, 0.0f, 100.0f);
    S.cleanliness = clampf(S.cleanliness - RATE_CLEANLINESS * mins, 0.0f, 100.0f);

    // Health follows the average of the four rather than being fed to the pet.
    const float drag = fminf(1.0f, HEALTH_DRAG_PER_MIN * mins);
    S.health = clampf(S.health + (averageCare() - S.health) * drag, 0.0f, 100.0f);

    const bool wasSick = has(PF_SICK);
    if (!wasSick && (S.health < SICK_AT_HEALTH || S.cleanliness < SICK_AT_CLEANLINESS)) {
        setFlag(PF_SICK, true);
        say("i feel sick...");
    } else if (wasSick && S.health > WELL_AT_HEALTH && S.cleanliness > WELL_AT_CLEANLINESS) {
        setFlag(PF_SICK, false);
        say("i feel better now");
    }

    if (averageCare() > CARE_GOOD_AVG) {
        S.carePoints += CARE_PER_MIN * mins;
    }
}

void GamePet::tick(uint32_t sec)
{
    S.ageSec += sec;
    decay(sec);

    if (S.stage < 3) {
        const StageGate &g = GATES[S.stage];
        if (S.ageSec >= g.ageSec && S.carePoints >= g.care) {
            S.stage++;
            say("i grew into a %s", STAGE_NAMES[S.stage]);
            setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC * 1.5f);
            save();
        }
    }
}

void GamePet::rollThoughts(uint32_t sec)
{
    (void)sec;
    if (has(PF_SICK))            say("%s", T_SICK[random(0, 3)]);
    else if (S.hunger < 30)      say("%s", T_HUNGRY[random(0, 3)]);
    else if (S.energy < 30)      say("%s", T_TIRED[random(0, 3)]);
    else if (S.cleanliness < 30) say("%s", T_DIRTY[random(0, 3)]);
    else if (S.happiness < 30)   say("%s", T_SAD[random(0, 3)]);
    else if (averageCare() > 75) say("%s", T_GOOD[random(0, 3)]);
    else                         say("%s", T_IDLE[random(0, 4)]);
}

void GamePet::act(int a)
{
    if (has(PF_ASLEEP) && a != ACT_SLEEP && a != ACT_NAME) {
        say("zzz...");
        setAnimation(ANIM_SLEEP, 0.0f);
        return;
    }

    switch (a) {
    case ACT_FEED:
        S.hunger    = clampf(S.hunger + 30.0f, 0, 100);
        S.happiness = clampf(S.happiness + 10.0f, 0, 100);
        S.feedCount++;
        say("mmm, thanks!");
        setAnimation(ANIM_EAT, ACTION_HOLD_SEC);
        break;
    case ACT_PLAY:
        S.happiness = clampf(S.happiness + 20.0f, 0, 100);
        S.energy    = clampf(S.energy    - 15.0f, 0, 100);
        S.attentions++;
        say("that was fun!");
        setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC);
        break;
    case ACT_PET:
        // A small, free gesture of affection: cheap on purpose, so noticing the pet is
        // always worth doing.
        S.happiness = clampf(S.happiness + 8.0f, 0, 100);
        S.attentions++;
        say("hehe");
        setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC);
        break;
    case ACT_SLEEP:
        setFlag(PF_ASLEEP, !has(PF_ASLEEP));
        if (has(PF_ASLEEP)) {
            say("goodnight...");
            setAnimation(ANIM_SLEEP, 0.0f);
        } else {
            say("morning!");
            setAnimation(ANIM_IDLE, 0.0f);
        }
        break;
    case ACT_BATHE:
        S.cleanliness = 100.0f;
        S.happiness = clampf(S.happiness + 5.0f, 0, 100);
        say("all clean!");
        setAnimation(ANIM_BATH, ACTION_HOLD_SEC);
        break;
    case ACT_NAME: {
        // No keyboard on a badge, so the pet is renamed by cycling a short list.
        int cur = 0;
        for (int i = 0; i < NAME_COUNT; i++) {
            if (strcmp(S.name, NAMES[i]) == 0) { cur = i; break; }
        }
        strncpy(S.name, NAMES[(cur + 1) % NAME_COUNT], sizeof(S.name) - 1);
        S.name[sizeof(S.name) - 1] = 0;
        say("call me %s", S.name);
        break;
    }
    }
    save();
}

// ------------------------------------------------------------------ drawing ----
void GamePet::drawIcon(float cx, float cy, float size) const
{
    // The face in miniature: two eyes and a smile, sized to the launcher tile.
    const int r  = (int)fmaxf(2.0f, size * 0.10f);
    const int dx = (int)(size * 0.22f);
    gfx::fillCircle(cx - dx, cy - size * 0.06f, (float)r, COL_ACCENT);
    gfx::fillCircle(cx + dx, cy - size * 0.06f, (float)r, COL_ACCENT);
    for (int i = -dx; i <= dx; i++) {
        gfx::fillRect((int)cx + i, (int)(cy + size * 0.16f) + abs(i) / 3, 1, 2, COL_ACCENT);
    }
}

void GamePet::drawMeter(const char *label, float value, int x, int y)
{
    char line[32];
    const int filled = (int)lroundf(clampf(value, 0, 100) / 100.0f * BAR_CHARS);
    int k = snprintf(line, sizeof(line), "%-6s [", label);
    for (int i = 0; i < BAR_CHARS; i++) line[k++] = (i < filled) ? '#' : '.';
    line[k++] = ']';
    line[k] = 0;

    // Labels stay dim and only the value carries colour, so a full bar and an empty
    // one are told apart at a glance.
    gfx::text(line, x, y, (value < 30.0f) ? COL_WARN : COL_ACCENT, 1);
}

void GamePet::drawFace()
{
    const AnimDef &a = ANIMS[anim];
    const FaceDef &f = a.frames[animFrame % a.count];

    const int cx = (int)SCR_CX;
    const int cy = FACE_CY;

    // Raised arms, as a kaomoji shows with its leading and trailing slashes.
    if (f.arms) {
        for (int i = 0; i < 14; i++) {
            gfx::fillRect(cx - EYE_DX - 12 - i, cy + EYE_DY + 6 + i, 3, 3, COL_ACCENT);
            gfx::fillRect(cx + EYE_DX + 10 + i, cy + EYE_DY + 6 + i, 3, 3, COL_ACCENT);
        }
    }

    drawEye(cx - EYE_DX, cy + EYE_DY, f.eyes, COL_ACCENT);
    drawEye(cx + EYE_DX, cy + EYE_DY, f.eyes, COL_ACCENT);
    drawMouth(cx, cy + MOUTH_DY, f.mouth, COL_ACCENT);
}

void GamePet::layoutMenu(float *xs, float *ys, float *ws, bool *enabled, int *count) const
{
    const int perRow = 3;
    const float gap = 16.0f;
    for (int i = 0; i < ACT_COUNT; i++) {
        const int row = i / perRow;
        const int first = row * perRow;
        const int last = (first + perRow > ACT_COUNT) ? ACT_COUNT : first + perRow;

        float total = 0.0f;
        for (int j = first; j < last; j++) total += gfx::textWidth(ACT_LABEL[j], 1) + gap;
        total -= gap;

        float x = SCR_CX - total * 0.5f;
        for (int j = first; j < last; j++) {
            const int w = gfx::textWidth(ACT_LABEL[j], 1);
            if (j == i) {
                xs[i] = x;
                ws[i] = (float)w;
                ys[i] = MENU_Y0 + row * MENU_PITCH;
            }
            x += w + gap;
        }
        // Sleeping blocks everything except waking and renaming; dim the rest so the
        // state is visible rather than only enforced.
        enabled[i] = !has(PF_ASLEEP) || i == ACT_SLEEP || i == ACT_NAME;
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

// -------------------------------------------------------------------- frame ----
void GamePet::enter()
{
    if (!petSessionId) petSessionId = (uint32_t)esp_random();

    // Measure the time off-screen before load() overwrites the saved clock. Only valid
    // within one boot: after a power cycle millis() has restarted and the gap is
    // unknowable, so the pet simply waits.
    const uint32_t nowMs = millis();
    load();

    uint32_t awaySec = 0;
    if (S.sessionId == petSessionId && nowMs > S.lastSeenMs) {
        awaySec = (nowMs - S.lastSeenMs) / 1000;
        if (awaySec > AWAY_CAP_SEC) awaySec = AWAY_CAP_SEC;
    }
    if (awaySec > 0) {
        Serial.printf("[pet] %u s passed off-screen; applying\n", (unsigned)awaySec);
        tick(awaySec);
        save();
    }
    S.sessionId = petSessionId;
    S.lastSeenMs = nowMs;

    anim = has(PF_ASLEEP) ? ANIM_SLEEP : (has(PF_SICK) ? ANIM_SICK : ANIM_IDLE);
    animFrame = 0;
    animFrameSec = 0.0f;
    animHoldSec = 0.0f;
    tickAccum = 0.0f;
    saveCounter = 0.0f;
    thoughtTimer = 0.0f;
    lastTapped = -1;
    setDirty(0, SCR_H - 1);
}

void GamePet::leave()
{
    save();
    gfx::clear(COL_BG);
}

void GamePet::update(float dt)
{
    const uint32_t ms = (uint32_t)(dt * 1000.0f + 0.5f);
    S.lastSeenMs = millis();

    // Advance the simulation in whole seconds: taking dt directly would round every
    // 16 ms frame down to zero and the pet would never age.
    tickAccum += dt;
    const uint32_t wholeSec = (uint32_t)tickAccum;
    if (wholeSec > 0) {
        tickAccum -= (float)wholeSec;
        tick(wholeSec);
    }

    // --- face ---------------------------------------------------------------
    animFrameSec += dt;
    if (animFrameSec >= FRAME_SEC) {
        animFrameSec = 0.0f;
        animFrame++;
    }
    if (animHoldSec > 0.0f) {
        animHoldSec -= dt;
    } else {
        // Drift back to the resting state, which also tracks being asleep or ill
        // without those needing to be forced every frame.
        const int rest = has(PF_ASLEEP) ? ANIM_SLEEP : (has(PF_SICK) ? ANIM_SICK : ANIM_IDLE);
        if (anim != rest) setAnimation(rest, 0.0f);
    }

    // --- thoughts -----------------------------------------------------------
    thoughtAgeSec += dt;
    thoughtTimer += ms;
    if (thoughtTimer >= THOUGHT_EVERY_MS) {
        thoughtTimer = 0;
        rollThoughts(0);
    }

    // --- touch --------------------------------------------------------------
    lastTapped = -1;
    if (input::tapped()) {
        float xs[ACT_COUNT], ys[ACT_COUNT], ws[ACT_COUNT];
        bool en[ACT_COUNT];
        int n = 0;
        layoutMenu(xs, ys, ws, en, &n);
        for (int i = 0; i < n; i++) {
            // Generous vertically and a few pixels wide: these are words rather than
            // tiles, and a finger is not a pointer.
            const float dx = input::tapX() - (xs[i] + ws[i] * 0.5f);
            const float dy = input::tapY() - (ys[i] + 3);
            if (fabsf(dx) <= ws[i] * 0.5f + 6.0f && fabsf(dy) <= 12.0f) {
                lastTapped = i;
                act(i);
                break;
            }
        }
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

    char head[48];
    snprintf(head, sizeof(head), "%s  |  %s  |  %02d:%02d", S.name, STAGE_NAMES[S.stage],
             (int)(S.ageSec / 3600) % 100, (int)(S.ageSec / 60) % 60);
    gfx::textCentered(head, HEAD_Y, COL_ACCENT, 1);
    gfx::fillRect(RULE_X0, RULE1_Y, RULE_X1 - RULE_X0, 1, COL_RULE);

    drawMeter("HUNGER", S.hunger,      METER_COL_A, METER_ROW0);
    drawMeter("ENERGY", S.energy,      METER_COL_B, METER_ROW0);
    drawMeter("HAPPY",  S.happiness,   METER_COL_A, METER_ROW1);
    drawMeter("CLEAN",  S.cleanliness, METER_COL_B, METER_ROW1);

    {
        char line[32];
        const int filled = (int)lroundf(clampf(S.health, 0, 100) / 100.0f * BAR_CHARS);
        int k = snprintf(line, sizeof(line), "%-6s [", "HEALTH");
        for (int i = 0; i < BAR_CHARS; i++) line[k++] = (i < filled) ? '#' : '.';
        line[k++] = ']';
        line[k] = 0;
        const int w = gfx::textWidth(line, 1);
        gfx::text(line, (int)(SCR_CX - w * 0.5f), HEALTH_Y, COL_TEXT, 1);
    }

    drawFace();

    if (thought[0]) {
        gfx::textCentered(thought, THOUGHT_Y, COL_TEXT, 1);
    }

    gfx::fillRect(RULE_X0, RULE2_Y, RULE_X1 - RULE_X0, 1, COL_RULE);
    drawMenu();
    setDirty(0, SCR_H - 1);
}
