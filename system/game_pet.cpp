#include "game_pet.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "Display_ST77916.h"
#include "esp_random.h"
#include "assets_icons.h"
#include "clock.h"
#include "assets_pet.h"
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

// ------------------------------------------------------------------ colours ----
static const uint16_t COL_BG     = PX(RGB565(0x0E, 0x0E, 0x16));
static const uint16_t COL_TEXT   = PX(RGB565(0xEC, 0xEC, 0xF4));
static const uint16_t COL_DIM    = PX(RGB565(0x6E, 0x6E, 0x80));
static const uint16_t COL_RULE   = PX(RGB565(0x2A, 0x2A, 0x3A));
static const uint16_t COL_ACCENT = PX(RGB565(0xD9, 0x77, 0x57));   // clawd's rust
static const uint16_t COL_WARN   = PX(RGB565(0xE0, 0x50, 0x50));
static const uint16_t COL_PANEL  = PX(RGB565(0x1A, 0x1A, 0x26));   // the speech box

// ------------------------------------------------------------------- the body ---
// The pet is clawd himself, not a cartoon face drawn in his place.
// tools/make_pet_sprites.py renders him at four sizes -- one per life stage, from
// the same artwork the rolling game uses -- so growing up is something you can see,
// and punches the eyes out of that artwork. Everything expressive is then drawn
// into that gap, which is what lets four body sprites carry seven moods.
//
// This replaced a fully drawn face. The face existed because kaomoji -- the
// original's (o.o) and friends -- are Unicode, and their 8x8 ASCII equivalents
// render as [ ^ _ ^ ]: at that size a parenthesis is a straight stroke. Drawn eyes
// on a real body get the expressiveness back without giving up the character.
enum { EYE_ROUND, EYE_HAPPY, EYE_SHUT, EYE_CROSS, EYE_ANGRY };
enum { EMOTE_NONE, EMOTE_HEART, EMOTE_ANGER, EMOTE_SICK, EMOTE_ZZZ };

// A mood is a face plus a movement: which eyes, whether a mouth shows, what floats
// over its shoulder, and how far it bobs. `bob` is an amplitude in pixels.
struct MoodDef { uint8_t eyes; uint8_t emote; bool mouth; uint8_t bob; };

static const MoodDef MOODS[ANIM_COUNT] = {
    /* IDLE    */ { EYE_ROUND, EMOTE_NONE,  false, 2 },
    /* HAPPY   */ { EYE_HAPPY, EMOTE_HEART, false, 4 },
    /* EAT     */ { EYE_SHUT,  EMOTE_NONE,  true,  3 },
    /* SLEEP   */ { EYE_SHUT,  EMOTE_ZZZ,   false, 0 },
    /* BATH    */ { EYE_HAPPY, EMOTE_HEART, false, 3 },
    /* SICK    */ { EYE_CROSS, EMOTE_SICK,  false, 0 },
    /* ANNOYED */ { EYE_ANGRY, EMOTE_ANGER, false, 4 },
};

static const float FRAME_SEC       = 0.45f;
static const float ACTION_HOLD_SEC = 2.6f;
static const float BOB_HZ          = 0.9f;

static void drawEye(int x, int y, int r, int style, uint16_t c, int dir = 1)
{
    switch (style) {
    case EYE_HAPPY:
        // An upward arc -- the ^ of a ^_^ smile, which is the one kaomoji glyph that
        // does survive at this size.
        for (int i = -r; i <= r; i++) {
            gfx::fillRect(x + i, y - (r - abs(i)) / 2, 1, 3, c);
        }
        break;
    case EYE_SHUT:
        gfx::fillRect(x - r, y - 1, 2 * r + 1, 2, c);
        break;
    case EYE_CROSS:
        gfx::line((float)(x - r), (float)(y - r), (float)(x + r), (float)(y + r), c);
        gfx::line((float)(x + r), (float)(y - r), (float)(x - r), (float)(y + r), c);
        break;
    case EYE_ANGRY:
        // Inner end down, outer end up: the V of a scowl. `dir` mirrors it, because
        // one call draws both eyes and only one of them leans each way. Curving it
        // through the middle instead reads as sad, which is the wrong message.
        for (int i = -r; i <= r; i++) {
            const int t = (dir > 0) ? (i + r) : (r - i);
            gfx::fillRect(x + i, y - r / 2 + t / 2, 1, 2, c);
        }
        break;
    default:
        gfx::fillCircle((float)x, (float)y, (float)r, c);
        break;
    }
}

static void drawMouth(int x, int y, int r, uint16_t c)
{
    // An open, chewing mouth: a half-disc under the eyes.
    for (int i = -r; i <= r; i++) {
        gfx::fillRect(x + i, y, 1, (r - abs(i)) * 3 / 4 + 1, c);
    }
}

// Writes one body sprite straight into the framebuffer. Going through fillRect for
// each pixel would be simpler and too slow: the adult body is 18816 pixels, and
// every frame repaints the whole screen anyway.
static void blitBody(const pet_sprite_t &s, int cx, int cy)
{
    const uint16_t body = PX(pet_palette[1]);
    const int x0 = cx - s.w / 2;
    const int y0 = cy - s.h / 2;
    for (int y = 0; y < s.h; y++) {
        const int dy = y0 + y;
        if ((unsigned)dy >= (unsigned)SCR_H) continue;
        const uint8_t *row = s.bits + y * s.rowBytes;
        uint16_t *out = fb + dy * SCR_W;
        for (int x = 0; x < s.w; x++) {
            if ((row[x >> 2] >> ((x & 3) * 2)) & 3) {
                const int dx = x0 + x;
                if ((unsigned)dx < (unsigned)SCR_W) out[dx] = body;
            }
        }
    }
}

// The marks that appear over clawd's shoulder. Drawn rather than stored as sprites:
// each is a handful of shapes, and at 20 px across a bitmap would cost about as many
// bytes as the pet's whole body.
static void drawHeart(float x, float y, float r, uint16_t c)
{
    gfx::fillCircle(x - r * 0.5f, y - r * 0.3f, r * 0.55f, c);
    gfx::fillCircle(x + r * 0.5f, y - r * 0.3f, r * 0.55f, c);
    for (int i = -(int)r; i <= (int)r; i++) {
        gfx::fillRect((int)x + i, (int)(y + r * 0.25f), 1,
                      (int)((r - abs(i)) * 0.9f), c);
    }
}

static void drawAngerMark(float x, float y, float r, uint16_t c)
{
    // Four strokes out of a centre: the cross-popping vein, which needs no
    // explanation to anyone who has watched a cartoon.
    for (int k = 0; k < 4; k++) {
        const float a = PI_F * 0.25f + (float)k * PI_F * 0.5f;
        // Two parallel strokes per arm: at this size a single one-pixel line reads
        // as a speck rather than as a vein.
        for (int t = 0; t < 2; t++) {
            const float o = (float)t * 0.22f + 0.11f;
            gfx::line(x + cosf(a) * r * 0.30f + sinf(a) * r * o,
                      y + sinf(a) * r * 0.30f - cosf(a) * r * o,
                      x + cosf(a) * r + sinf(a) * r * o,
                      y + sinf(a) * r - cosf(a) * r * o, c);
        }
    }
}

static void drawSickMark(float x, float y, float r, uint16_t c)
{
    gfx::fillRect((int)(x - r * 0.28f), (int)(y - r), (int)(r * 0.56f), (int)(2 * r), c);
    gfx::fillRect((int)(x - r), (int)(y - r * 0.28f), (int)(2 * r), (int)(r * 0.56f), c);
}

static void drawEmote(uint8_t emote, float x, float y)
{
    switch (emote) {
    case EMOTE_HEART: drawHeart(x, y, 9.0f, COL_WARN); break;
    case EMOTE_ANGER: drawAngerMark(x, y, 9.0f, COL_WARN); break;
    case EMOTE_SICK:  drawSickMark(x, y, 7.0f, COL_WARN); break;
    case EMOTE_ZZZ:
        gfx::text("z", (int)x - 6, (int)y - 12, COL_TEXT, 2);
        gfx::text("z", (int)x + 8, (int)y, COL_DIM, 1);
        break;
    default: break;
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
static const char *const T_GOOD[]   = { "life is good", "i like it here", "thanks for the care" };
static const char *const T_IDLE[]   = { "just hanging out", "nice day", "watching the pixels", "hello!" };

static const char *const NAMES[] = { "CLAWD", "PIP", "MOMO", "BOB", "ZIGGY", "NOVA" };
static const int NAME_COUNT = (int)(sizeof(NAMES) / sizeof(NAMES[0]));

// ------------------------------------------------------------------ layout -----
static const int HEAD_Y  = 24;
static const int RULE1_Y = 38;
static const int METER_COL_A = 54, METER_COL_B = 196;
static const int METER_ROW0  = 50,  METER_ROW1 = 64;
static const int HEALTH_Y    = 78;

// clawd stays in the middle, and what he says goes over his head: a ringed bubble
// centred on the screen's own axis. A rectangle beside him was the wrong shape for a
// round panel and pushed him off centre to make room for it.
//
// The top block is compressed to pay for the bubble -- the header up to 28 and the
// meter rows to 54 and 68, all a line's worth tighter than they were.
static const int BUBBLE_CY   = 110;
static const int BUBBLE_RX   = 95;    // widest the bubble gets
static const int BUBBLE_RXMIN = 34;
static const int BUBBLE_TAIL = 5;     // how far the point hangs below the ring
static const int BUBBLE_RYMIN = 15;   // so a one-line bubble is not a sliver
static const int SPEECH_PAD  = 8;
// Deliberately narrow: a wider box fits more thoughts on one line and comes out as a
// flat ellipse, which reads as a bar rather than as a bubble. Two lines make it round.
static const int SPEECH_COLS = 16;
static const int SPEECH_LINE = 11;    // 8 px glyphs plus leading
static const int SPEECH_MAXLINES = 2;

static const int PET_CX      = 180;
static const int PET_CY      = 184;

// The menu follows the bezel: six items on an arc across the bottom. Two rows of
// three put the entries about 30 px apart with 8 px of glyph to hit, which is not a
// target a finger can find. On an arc at this radius they are 51 px apart, each with
// a 24 px grab radius of its own, and the arc stops short of the pet.
static const float MENU_R    = 134.0f;
static const float MENU_SPAN = 110.0f;    // degrees of arc, centred on straight down
static const float MENU_HIT  = 24.0f;

// The header rule's span. Not the full width: at y=38 the bezel leaves only about
// x=69..291 visible, and a line drawn past that just ends under the rim.
static const int RULE_X0 = 76, RULE_X1 = 284;
static const int CHAR_W  = 7;                   // the font's advance at scale 1
static const int BAR_CHARS = 8;       // '#' filled, '.' empty

static const char *const ACT_LABEL[ACT_COUNT] = { "FEED", "PLAY", "PET", "SLEEP", "SCRUB", "NAME" };

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
        // Cleaning is the shake game rather than a bar that moves by itself.
        startShakeGame();
        return;   // startShakeGame saves its own outcome
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
    // Headphones: the animation where clawd settles in and listens, which is what a
    // cared-for pet looks like. Distinct in silhouette and in colour from the other
    // two entries' props (see tools/make_icon_sprites.py).
    gfx::drawSpriteSized(icon_pet, cx, cy, 0.0f, size, icon_pet_palette);
}

// Splits the thought to fit the speech box. The font is fixed-width, so counting
// characters is exact. Breaks at a space where there is one and mid-word where a
// single word is longer than the line.
static int wrapThought(const char *s, char out[][24], int maxLines, int cols)
{
    if (cols < 4) cols = 4;
    int n = 0;
    while (*s && n < maxLines) {
        while (*s == ' ') s++;
        const int len = (int)strlen(s);
        if (len == 0) break;
        if (len <= cols) {
            snprintf(out[n], 24, "%s", s);
            return n + 1;
        }
        int cut = cols;
        for (int i = cols; i > 0; i--) {
            if (s[i] == ' ') { cut = i; break; }
        }
        memcpy(out[n], s, (size_t)cut);
        out[n][cut] = 0;
        n++;
        s += cut;
    }
    return n;
}

// A filled ellipse with its top and bottom edges drawn as a ring. gfx has no ellipse
// because nothing else has wanted one: the rest of the screen is rectangles, circles
// and the menu's arc.
static void ellipsePanel(int cx, int cy, int rx, int ry, int tail, uint16_t fill,
                         uint16_t edge)
{
    for (int x = -rx; x <= rx; x++) {
        const float t = (float)x / (float)rx;
        const int h = (int)((float)ry * sqrtf(1.0f - t * t) + 0.5f);
        if (h <= 0) continue;
        gfx::fillRect(cx + x, cy - h, 1, h * 2, fill);
        gfx::pixel(cx + x, cy - h, edge);
        // Leave a gap in the bottom edge where the point hangs off it.
        if (abs(x) > tail) gfx::pixel(cx + x, cy + h - 1, edge);
    }
    if (tail <= 0) return;
    for (int k = 0; k < BUBBLE_TAIL; k++) {
        const int half = tail - k * tail / BUBBLE_TAIL;
        gfx::fillRect(cx - half, cy + ry + k, half * 2 + 1, 1, fill);
        gfx::pixel(cx - half - 1, cy + ry + k, edge);
        gfx::pixel(cx + half + 1, cy + ry + k, edge);
    }
}

void GamePet::drawThought()
{
    if (!thought[0]) return;

    char lines[SPEECH_MAXLINES][24];
    const int n = wrapThought(thought, lines, SPEECH_MAXLINES, SPEECH_COLS);
    if (n <= 0) return;

    int widest = 0;
    for (int i = 0; i < n; i++) {
        const int w = (int)strlen(lines[i]) * CHAR_W;
        if (w > widest) widest = w;
    }
    // Semi-axes measured to the text rather than guessed, so a short thought gets a
    // small bubble and a long one gets a wide one.
    int rx = widest / 2 + SPEECH_PAD + 4;
    if (rx < BUBBLE_RXMIN) rx = BUBBLE_RXMIN;
    if (rx > BUBBLE_RX) rx = BUBBLE_RX;
    int ry = (n * SPEECH_LINE) / 2 + SPEECH_PAD;
    if (ry < BUBBLE_RYMIN) ry = BUBBLE_RYMIN;
    const int tail = 7;

    ellipsePanel(PET_CX, BUBBLE_CY, rx, ry, tail, COL_PANEL, gfx::dim(COL_ACCENT, 0.55f));

    const int y0 = BUBBLE_CY - (n * SPEECH_LINE) / 2;
    for (int i = 0; i < n; i++) {
        const int w = (int)strlen(lines[i]) * CHAR_W;
        gfx::text(lines[i], PET_CX - w / 2, y0 + i * SPEECH_LINE, COL_TEXT, 1);
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

void GamePet::drawPet()
{
    const MoodDef &m = MOODS[anim];
    const pet_sprite_t &s = pet_sprites[S.stage < PET_STAGE_COUNT ? S.stage : PET_STAGE_COUNT - 1];

    // A quick wobble while the pet is cross, so being poked too often has a physical
    // tell and not only a change of face.
    const int jx = (jitter > 0.0f) ? (int)(sinf(jitter * 45.0f) * 5.0f) : 0;
    const int bob = (m.bob > 0) ? (int)lroundf(sinf(bobPhase * 2.0f * PI_F * BOB_HZ) * m.bob) : 0;

    const int cx = (int)petX + jx;
    const int cy = (int)petY + bob;

    blitBody(s, cx, cy);

    // The eyes are the pet's own, drawn into the gap the artwork left for them, so
    // one body covers every mood. They lean towards the last touch, which is cheap
    // and reads as paying attention.
    const int lx = (int)lroundf(lookX);
    const int ly = (int)lroundf(lookY);
    const int ey = cy + s.eyeDY + ly;
    uint8_t eyes = m.eyes;
    // An occasional blink, counted off the animation's own frame clock rather than a
    // separate timer. Blinking overrides the mood for a single frame.
    if (anim == ANIM_IDLE && (animFrame % 4) == 3) eyes = EYE_SHUT;

    const uint16_t ink = PX(pet_palette[2]);
    // eyeDX is the distance from the sprite's centre to one eye, not a coordinate,
    // and the glance is added after that placement rather than folded into it. Adding
    // it to the distance instead moved the left eye by -lx and the right by +lx: the
    // two eyes slid apart as the finger moved, which reads as a squint rather than as
    // looking at it.
    drawEye(cx - s.eyeDX + lx, ey, s.eyeR, eyes, ink, +1);
    drawEye(cx + s.eyeDX + lx, ey, s.eyeR, eyes, ink, -1);
    if (m.mouth) drawMouth(cx + lx / 2, ey + s.eyeR * 2, s.eyeR, ink);

    // The mood mark sits at the pet's shoulder, clear of the meters above.
    if (m.emote != EMOTE_NONE) {
        drawEmote(m.emote, (float)(cx + s.w / 2 - 10), (float)(cy - s.h / 2 + 6));
    }
}

// The pet's touch target: an ellipse over the body, plus slack, because a finger is
// not a pointer. Deliberately generous -- this is the thing the user is meant to
// grab -- but not a circle, which at the adult width would swallow the menu below.
bool GamePet::petHit(int x, int y) const
{
    const pet_sprite_t &s = pet_sprites[S.stage < PET_STAGE_COUNT ? S.stage : PET_STAGE_COUNT - 1];
    const float rx = s.w * 0.5f + 8.0f;
    const float ry = s.h * 0.5f + 8.0f;
    const float dx = (float)x - petX;
    const float dy = (float)y - petY;
    return (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry) <= 1.0f;
}

void GamePet::drawDirt()
{
    // Blobs stuck to the pet, placed on a ring so they never hide its face.
    const pet_sprite_t &s = pet_sprites[S.stage < PET_STAGE_COUNT ? S.stage : PET_STAGE_COUNT - 1];
    const float rx = s.w * 0.30f + 14.0f;
    const float ry = s.h * 0.30f + 14.0f;
    for (int i = 0; i < dirt; i++) {
        const float ang = -PI_F * 0.5f + (float)i * (2.0f * PI_F / 5.0f);
        gfx::fillCircle(petX + cosf(ang) * rx, petY + sinf(ang) * ry + 4.0f, 5.0f, COL_DIM);
    }
}

void GamePet::startShakeGame()
{
    mode = MODE_SHAKE;
    dirt = 5;
    shakeIdle = 0.0f;
    shakeRefractory = 0.0f;
    accelPrevMag = 1.0f;
    say("shake it off!");
    setAnimation(ANIM_BATH, 0.0f);
}

void GamePet::finishShakeGame(bool completed)
{
    mode = MODE_PET;
    // Partial credit: whatever came off still counts, so a short attempt is never
    // wasted. Finishing the job is what the pet actually thanks you for.
    const float share = (5 - (float)dirt) / 5.0f;
    S.cleanliness = clampf(S.cleanliness + share * 55.0f, 0, 100);
    if (completed) {
        S.happiness = clampf(S.happiness + 10.0f, 0, 100);
        say("all clean!");
        setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC);
    } else {
        say("that will do");
    }
    dirt = 0;
    save();
}

void GamePet::updateShakeGame(float dt)
{
    // A shake is a spike in the magnitude of acceleration. The resting magnitude is
    // 1 g, so anything well past that is motion rather than orientation.
    float a[3];
    // input::tiltX/Y are trimmed and deadzoned, which is wrong for detecting a spike,
    // so the raw read is taken here instead.
    input::readRawAccel(a);
    const float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);

    if (shakeRefractory > 0.0f) shakeRefractory -= dt;
    if (accelMute > 0.0f) accelMute -= dt;

    const float delta = fabsf(mag - accelPrevMag);
    accelPrevMag = mag;

    if (accelMute <= 0.0f && shakeRefractory <= 0.0f && delta > 0.9f) {
        shakeRefractory = 0.18f;      // one shake must not count twice
        if (dirt > 0) {
            dirt--;
            say(dirt ? "keep going!" : "done!");
            if (dirt == 0) {
                finishShakeGame(true);
                return;
            }
        }
    }

    shakeIdle += dt;
    if (shakeIdle > 20.0f) {
        finishShakeGame(false);       // gives up rather than trapping the player
    }
}

void GamePet::menuSlot(int i, float *sx, float *sy) const
{
    // Angle measured from straight down, so the arc is symmetric about it and the
    // items run left to right in menu order.
    const float step = MENU_SPAN / (float)(ACT_COUNT - 1);
    const float a = (-MENU_SPAN * 0.5f + step * (float)i) * PI_F / 180.0f;
    *sx = SCR_CX + sinf(a) * MENU_R;
    *sy = SCR_CY + cosf(a) * MENU_R;
}

bool GamePet::menuEnabled(int i) const
{
    // Sleeping blocks everything except waking and renaming.
    return !has(PF_ASLEEP) || i == ACT_SLEEP || i == ACT_NAME;
}

void GamePet::drawMenu()
{
    for (int i = 0; i < ACT_COUNT; i++) {
        float sx, sy;
        menuSlot(i, &sx, &sy);
        const int w = gfx::textWidth(ACT_LABEL[i], 1);
        const int x = (int)(sx - w * 0.5f);
        const int y = (int)(sy - 4.0f);

        // A faint disc marks the grab area, so the target is visible as well as
        // bigger than the word in it.
        gfx::strokeCircle(sx, sy, MENU_HIT * 0.82f, 1.0f,
                          gfx::dim(COL_ACCENT, menuEnabled(i) ? 0.45f : 0.15f));
        if (i == lastTapped) {
            gfx::fillCircle(sx, sy, MENU_HIT * 0.82f, gfx::dim(COL_ACCENT, 0.55f));
        }
        gfx::text(ACT_LABEL[i], x, y,
                  menuEnabled(i) ? (i == lastTapped ? COL_TEXT : COL_ACCENT) : COL_DIM, 1);
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
    bobPhase = 0.0f;
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

    // The eyes follow the finger whenever it is down, fading back when it lifts.
    //
    // Not while it is being dragged, though: then the body is at the finger, so the
    // offset comes out near zero and only jitters with the per-frame difference
    // between the finger and where the body had got to. There is nothing to look at
    // when you are being carried.
    if (input::touchDown() && !dragging) {
        lookX = clampf((input::tapX() - petX) * 0.12f, -5.0f, 5.0f);
        lookY = clampf((input::tapY() - petY) * 0.12f, -5.0f, 5.0f);
    } else {
        lookX *= 1.0f - fminf(1.0f, 4.0f * dt);
        lookY *= 1.0f - fminf(1.0f, 4.0f * dt);
    }

    if (mode == MODE_SHAKE) {
        // Any tap leaves the game, so it can never trap the player.
        if (input::tapped()) {
            finishShakeGame(dirt == 0);
        } else {
            updateShakeGame(dt);
        }
    } else {
        // Holding on the pet picks it up. The touch is captured on press rather than
        // on release, so a drag that ends off the pet still counts as a drag.
        if (input::touchDown() && petHit(input::tapX(), input::tapY())
            && input::touchHeldMs() > 200) {
            dragging = true;
        }
        if (dragging) {
            petX = clampf((float)input::tapX(), 84.0f, 276.0f);
            petY = clampf((float)input::tapY(), 126.0f, 240.0f);
        } else {
            // Drifts back to its resting place.
            petX += ((float)PET_CX - petX) * fminf(1.0f, 2.5f * dt);
            petY += ((float)PET_CY - petY) * fminf(1.0f, 2.5f * dt);
        }

        if (input::tapped()) {
            // Touching the panel spikes the accelerometer, so the IMU is ignored for a
            // moment afterwards rather than being read as a shake.
            accelMute = 0.25f;
            if (dragging) {
                dragging = false;               // putting it down is not a poke
            } else if (petHit(input::tapX(), input::tapY())) {
                handlePetTouch();
            } else {
                handleMenuTouch();
            }
        }
    }

    // Poke heat cools off, so a burst of taps annoys while a steady one does not.
    if (pokeCool > 0.0f) {
        pokeCool -= dt;
        if (pokeCool <= 0.0f) pokeHeat = 0.0f;
    }
    if (jitter > 0.0f) jitter -= dt;
    bobPhase += dt;

    saveCounter += ms;
    if (saveCounter >= SAVE_PERIOD_MS) {
        saveCounter = 0.0f;
        save();
    }
}

void GamePet::handlePetTouch()
{
    pokeHeat += 1.0f;
    pokeCool = 1.2f;

    if (pokeHeat >= 3.0f) {
        // Rough handling. Losing a little happiness is the point: poking is not the
        // same as petting, and the pet says so.
        S.happiness = clampf(S.happiness - 2.0f, 0, 100);
        say("hey, that is enough");
        setAnimation(ANIM_ANNOYED, ACTION_HOLD_SEC);
        jitter = 0.6f;
    } else {
        S.happiness = clampf(S.happiness + 6.0f, 0, 100);
        S.attentions++;
        say("hehe");
        setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC);
    }
    save();
}

void GamePet::handleMenuTouch()
{
    // Nearest slot wins if the touch is inside any grab radius, rather than the first
    // one that matches: on an arc the ends are closer to their neighbours' labels than
    // they are to their own centre, and taking the nearest is what the finger meant.
    int best = -1;
    float bestD2 = MENU_HIT * MENU_HIT;
    for (int i = 0; i < ACT_COUNT; i++) {
        float sx, sy;
        menuSlot(i, &sx, &sy);
        const float dx = (float)input::tapX() - sx;
        const float dy = (float)input::tapY() - sy;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { best = i; bestD2 = d2; }
    }
    if (best >= 0 && menuEnabled(best)) {
        lastTapped = best;
        act(best);
    }
}

void GamePet::render()
{
    gfx::clear(COL_BG);

    char head[48];
    snprintf(head, sizeof(head), "%s  |  %s  |  %s", S.name, STAGE_NAMES[S.stage],
             sysclock::timeText());
    gfx::textCentered(head, HEAD_Y, COL_ACCENT, 1);
    gfx::fillRect(RULE_X0, RULE1_Y, RULE_X1 - RULE_X0, 1, COL_RULE);

    if (mode == MODE_SHAKE) {
        renderShakeGame();
        setDirty(0, SCR_H - 1);
        return;
    }

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

    drawPet();
    drawThought();
    drawMenu();
    setDirty(0, SCR_H - 1);
}

void GamePet::renderShakeGame()
{
    // The meters give way to the game: what matters while it runs is how much dirt is
    // left and what to do about it.
    char line[32];
    snprintf(line, sizeof(line), "SHAKE  %u LEFT", (unsigned)dirt);
    gfx::textCentered(line, METER_ROW0, dirt ? COL_ACCENT : COL_TEXT, 1);
    gfx::textCentered("hold the badge and shake it", METER_ROW1 + 4, COL_DIM, 1);

    // A health-style bar showing progress, reusing the same idiom as the meters.
    {
        const int done = 5 - (int)dirt;
        char bar[32];
        int k = snprintf(bar, sizeof(bar), "DIRT   [");
        for (int i = 0; i < BAR_CHARS; i++) bar[k++] = (i < done) ? '#' : '.';
        bar[k++] = ']';
        bar[k] = 0;
        const int w = gfx::textWidth(bar, 1);
        gfx::text(bar, (int)(SCR_CX - w * 0.5f), HEALTH_Y, COL_ACCENT, 1);
    }

    drawDirt();
    drawPet();
    drawThought();
    gfx::textCentered("tap to stop", SCR_H - 22, COL_DIM, 1);
}
