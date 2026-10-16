#include "game_pet.h"

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "assets_mon.h"
#include "assets_icons.h"
#include "clock.h"
#include "esp_random.h"
#include "gfx.h"
#include "input.h"
#include "sound.h"
#include "outfits.h"
#ifdef ARDUINO_ARCH_ESP32
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#endif

using namespace mon;

// --------------------------------------------------------------- the numbers ---
// The needs are the pet's, at the pet's rates: a bar left alone empties in about an
// hour and a half, which suits a badge that is picked up for ten minutes at a time.
static const float RATE_HUNGER      = 1.2f;
static const float RATE_HAPPINESS   = 0.9f;
static const float RATE_ENERGY      = 0.7f;
static const float RATE_CLEANLINESS = 0.6f;
// Sleep is the only thing that restores energy, so it has to be fast enough to be worth
// doing. At two points a minute a nap from empty to full is half an hour of watching a bar
// not move, which is not a mechanic but a punishment; nine fills it in about twelve
// minutes, which is a decision with a cost you can actually feel.
static const float SLEEP_ENERGY_PER_MIN = 9.0f;
static const float SLEEP_HUNGER_SCALE   = 0.5f;
static const float HEALTH_DRAG_PER_MIN  = 0.10f;

static const float SICK_AT_HEALTH      = 30.0f;
static const float SICK_AT_CLEANLINESS = 20.0f;
static const float WELL_AT_HEALTH      = 60.0f;
static const float WELL_AT_CLEANLINESS = 50.0f;

static const float CARE_PER_MIN  = 1.0f;
static const float CARE_GOOD_AVG = 70.0f;

// The egg. Warmth is the state of the needs rather than a separate thing to mind: a
// cold, grubby egg does not progress, which is why the needs are load-bearing before
// there is a creature to look after. About three minutes of a warm egg hatches it,
// which is short on purpose -- an egg that takes a day is an egg that gets skipped.
static const float HATCH_WARM_ENERGY = 40.0f;
static const float HATCH_WARM_CLEAN  = 40.0f;
static const float HATCH_PER_MIN     = 34.0f;
static const float HATCH_PER_POKE    = 4.0f;
static const int   HATCH_CRACK1      = 34;   // percent at which the shell starts to fail
static const int   HATCH_CRACK2      = 67;

// The two branch tracks. Both always creep up, so the branch is decided by which one
// is *ahead* rather than by whether the player remembered to press a particular button
// once. Resting and scrubbing feed vigor; playing and being poked feed joy.
static const float TRACK_BASE      = 0.35f;   // what a neglected mon still accrues
static const float VIGOR_REST_MIN  = 0.6f;    // per minute asleep
static const float VIGOR_SCRUB     = 3.0f;    // for a finished shake game
static const float JOY_PLAY        = 1.5f;
static const float JOY_PET         = 1.0f;

// The gates are deliberately short. A badge is picked up for ten minutes at a time, so
// an evolution that needs an hour of good care is an evolution that never happens; these
// are sized so that one focused session reaches the child form and a second reaches an
// adult one.
struct Gate { uint32_t ageSec; float care; };
static const Gate GATE_CHILD = {   5UL * 60,  6.0f };
static const Gate GATE_ADULT = {  20UL * 60, 25.0f };
static const float NEGLECT_CARE_AVG = 45.0f;   // below this, the adult form is DUSK

// A branch is "balanced" -- and so takes the rare form -- when the two tracks are
// within this fraction of their sum of each other.
static const float BALANCE_FRAC = 0.20f;

static const uint32_t SAVE_PERIOD_MS   = 15000;
static const uint32_t THOUGHT_EVERY_MS = 40000;
// How long a thought stays on screen. thoughtAgeSec has always been counted but nothing
// read it, so the bubble took up residence over the mon's head forever and only changed
// its text. It should be a remark, not a fixture.
static const float THOUGHT_SHOW_SEC    = 12.0f;
static const uint32_t AWAY_CAP_SEC     = 4UL * 3600;

static const char *const NVS_NAMESPACE = "clawdpet2";
static const uint32_t SAVE_MAGIC = 0x4D4F4E31;   // 'MON1'

static uint32_t monSessionId = 0;

// ------------------------------------------------------------------ colours ----
static const uint16_t COL_BG     = PX(RGB565(0x0E, 0x0E, 0x16));
static const uint16_t COL_TEXT   = PX(RGB565(0xEC, 0xEC, 0xF4));
static const uint16_t COL_DIM    = PX(RGB565(0x6E, 0x6E, 0x80));
static const uint16_t COL_RULE   = PX(RGB565(0x2A, 0x2A, 0x3A));
static const uint16_t COL_ACCENT = PX(RGB565(0xD9, 0x77, 0x57));
static const uint16_t COL_WARN   = PX(RGB565(0xE0, 0x50, 0x50));
static const uint16_t COL_PANEL  = PX(RGB565(0x1A, 0x1A, 0x26));
static const uint16_t COL_SHELL  = PX(RGB565(0xE6, 0xDF, 0xF0));
static const uint16_t COL_SHELL_EDGE = PX(RGB565(0x8E, 0x88, 0xA8));
static const uint16_t COL_CRACK      = PX(RGB565(0x4A, 0x44, 0x5C));

// ------------------------------------------------------------------- the face ---
// One body per form carries every mood the same way the pet's do: the artwork's eyes
// are punched out by the generator and redrawn here each frame.
enum { EYE_ROUND, EYE_HAPPY, EYE_SHUT, EYE_CROSS, EYE_ANGRY };
enum { EMOTE_NONE, EMOTE_HEART, EMOTE_ANGER, EMOTE_SICK, EMOTE_ZZZ, EMOTE_SPARK };

struct MoodDef { uint8_t eyes; uint8_t emote; bool mouth; uint8_t bob; };

static const MoodDef MOODS[ANIM_COUNT] = {
    /* IDLE    */ { EYE_ROUND, EMOTE_NONE,  false, 2 },
    /* HAPPY   */ { EYE_HAPPY, EMOTE_HEART, false, 4 },
    /* EAT     */ { EYE_SHUT,  EMOTE_NONE,  true,  3 },
    /* SLEEP   */ { EYE_SHUT,  EMOTE_ZZZ,   false, 0 },
    /* BATH    */ { EYE_HAPPY, EMOTE_SPARK, false, 3 },
    /* SICK    */ { EYE_CROSS, EMOTE_SICK,  false, 0 },
    /* ANNOYED */ { EYE_ANGRY, EMOTE_ANGER, false, 4 },
};

static const float FRAME_SEC       = 0.45f;
static const float ACTION_HOLD_SEC = 2.6f;
static const float BOB_HZ          = 0.9f;

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static const mon_form_t &formOf(uint8_t form)
{
    return mon_forms[form < MON_FORM_COUNT ? form : MON_FORM_COUNT - 1];
}

static void drawEye(int x, int y, int r, int style, uint16_t c, int dir = 1)
{
    switch (style) {
    case EYE_HAPPY:
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
    for (int i = -r; i <= r; i++) {
        gfx::fillRect(x + i, y, 1, (r - abs(i)) * 3 / 4 + 1, c);
    }
}

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
    for (int k = 0; k < 4; k++) {
        const float a = PI_F * 0.25f + (float)k * PI_F * 0.5f;
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

static void drawSpark(float x, float y, float r, uint16_t c)
{
    // A four-pointed sparkle: the bath is over and it is clean, which the heart alone
    // would claim too often.
    for (int k = 0; k < 4; k++) {
        const float a = (float)k * PI_F * 0.5f;
        gfx::line(x, y, x + cosf(a) * r, y + sinf(a) * r, c);
    }
    gfx::fillCircle(x, y, 2.0f, c);
}

static void drawEmote(uint8_t emote, float x, float y)
{
    switch (emote) {
    case EMOTE_HEART: drawHeart(x, y, 9.0f, COL_WARN); break;
    case EMOTE_ANGER: drawAngerMark(x, y, 9.0f, COL_WARN); break;
    case EMOTE_SICK:  drawSickMark(x, y, 7.0f, COL_WARN); break;
    case EMOTE_SPARK: drawSpark(x, y, 8.0f, COL_ACCENT); break;
    case EMOTE_ZZZ:
        gfx::text("z", (int)x - 6, (int)y - 12, COL_TEXT, 2);
        gfx::text("z", (int)x + 8, (int)y, COL_DIM, 1);
        break;
    default: break;
    }
}

// Writes one body straight into the framebuffer. Going through gfx::pixel for each
// pixel would be simpler and too slow: the adult body is 14652 pixels and every frame
// repaints the whole screen.
static void blitForm(const mon_form_t &f, int cx, int cy)
{
    const uint16_t body = PX(f.body);
    const uint16_t edge = PX(f.edge);
    const int x0 = cx - f.w / 2;
    const int y0 = cy - f.h / 2;
    for (int y = 0; y < f.h; y++) {
        const int dy = y0 + y;
        if ((unsigned)dy >= (unsigned)SCR_H) continue;
        const uint8_t *row = f.bits + y * f.rowBytes;
        uint16_t *out = fb + dy * SCR_W;
        for (int x = 0; x < f.w; x++) {
            const uint8_t v = (row[x >> 2] >> ((x & 3) * 2)) & 3;
            if (!v) continue;
            const int dx = x0 + x;
            if ((unsigned)dx >= (unsigned)SCR_W) continue;
            out[dx] = (v == 2) ? edge : body;
        }
    }
}

// ------------------------------------------------------------ what it says -----
static const char *const T_SICK[]   = { "i feel awful", "everything hurts", "medic..." };
static const char *const T_HUNGRY[] = { "feed me?", "my stomach growls", "so hungry" };
static const char *const T_TIRED[]  = { "so sleepy...", "i need rest", "eyes are heavy" };
static const char *const T_DIRTY[]  = { "i need a bath", "feeling grubby", "something is stuck to me" };
static const char *const T_SAD[]    = { "play with me?", "it is boring", "you ignoring me?" };
static const char *const T_GOOD[]   = { "i feel great", "best day ever", "thanks for the care" };
static const char *const T_IDLE[]   = { "just hanging out", "nice day", "watching the pixels", "hi!" };
static const char *const T_EGG[]    = { "...", "(a faint knock from inside)", "the shell is warm" };

static const char *const NAMES[] = { "PIP", "MOSS", "BUBBLE", "SPARK", "GRUB", "NOVA" };
static const int NAME_COUNT = (int)(sizeof(NAMES) / sizeof(NAMES[0]));

// ------------------------------------------------------------------ layout -----
static const int HEAD_Y  = 24;
static const int RULE1_Y = 38;
static const int METER_COL_A = 54, METER_COL_B = 196;
static const int METER_ROW0  = 50,  METER_ROW1 = 64;
static const int HEALTH_Y    = 78;
static const int DEX_Y       = SCR_H - 22;

static const int BUBBLE_CY   = 110;
static const int BUBBLE_RX   = 95;
static const int BUBBLE_RXMIN = 34;
static const int BUBBLE_TAIL = 5;
static const int BUBBLE_RYMIN = 15;
static const int SPEECH_PAD  = 8;
static const int SPEECH_COLS = 16;
static const int SPEECH_LINE = 11;
static const int SPEECH_MAXLINES = 2;

static const int PET_CX      = 180;
static const int PET_CY      = 184;
static const int EGG_RX      = 34;
static const int EGG_RY      = 44;

static const float MENU_R    = 134.0f;
static const float MENU_SPAN = 110.0f;
static const float MENU_HIT  = 24.0f;

static const int RULE_X0 = 76, RULE_X1 = 284;
static const int CHAR_W  = 7;
static const int BAR_CHARS = 6;

static const char *const ACT_LABEL[ACT_COUNT] = { "FEED", "PLAY", "PET", "REST", "SCRUB", "STYLE" };

// The sixth slot opens the wardrobe; its secondary action retains rename/new egg.
static const char *actLabelFor(int i, uint8_t)
{
    return ACT_LABEL[i];
}

// ------------------------------------------------------------------ helpers ----
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

void GamePet::react(const char *emotion)
{
    if (!emotion || has(MF_ASLEEP)) return;
    int a = ANIM_IDLE;
    if (!strcmp(emotion, "happy") || !strcmp(emotion, "laughing") ||
        !strcmp(emotion, "loving") || !strcmp(emotion, "excited")) a = ANIM_HAPPY;
    else if (!strcmp(emotion, "angry") || !strcmp(emotion, "annoyed")) a = ANIM_ANNOYED;
    else if (!strcmp(emotion, "sad") || !strcmp(emotion, "crying")) a = ANIM_SICK;
    else if (!strcmp(emotion, "sleepy")) a = ANIM_SLEEP;
    setAnimation(a, ACTION_HOLD_SEC);
}

void GamePet::describe(char *out, size_t size) const
{
    snprintf(out, size, "You are CLAWD PET, the creature on this badge. Name: %.11s. "
        "Stage: %s. Hunger/fullness %.0f, happiness %.0f, energy %.0f, cleanliness %.0f "
        "(all 0-100, higher is better). Asleep: %s. Sick: %s. Speak briefly as this pet.",
        S.name, S.stage == STAGE_EGG ? "egg" : formOf(S.form).name,
        S.hunger, S.happiness, S.energy, S.cleanliness,
        has(MF_ASLEEP) ? "yes" : "no", has(MF_SICK) ? "yes" : "no");
}

void GamePet::discover(int form)
{
    S.seen |= (uint8_t)(1u << form);
}

// -------------------------------------------------------------------- NVS -----
void GamePet::load()
{
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) {
        MonSave blob;
        const size_t got = prefs.isKey("blob") ? prefs.getBytes("blob", &blob, sizeof(blob)) : 0;
        if (got == sizeof(blob) && blob.magic == SAVE_MAGIC) {
            S = blob.state;
            prefs.end();
            if (sane()) {
                Serial.printf("[mon] restored: %s stage=%u form=%u seen=%u/7\n",
                              S.name, (unsigned)S.stage, (unsigned)S.form,
                              (unsigned)__builtin_popcount(S.seen));
                return;
            }
            Serial.println("[mon] save looked corrupt; starting a new egg");
        } else {
            prefs.end();
        }
    }
    if (restoreLegacy()) { save(); return; }
    S = {};
    resetToEgg();
    Serial.println("[mon] no save found; an egg appeared");
}

bool GamePet::sane() const
{
    const float values[] = { S.hunger, S.happiness, S.energy, S.cleanliness,
        S.health, S.hatch, S.carePoints, S.joy, S.vigor, S.careSum };
    for (float v : values) if (!isfinite(v) || v < 0.0f) return false;
    return S.stage <= STAGE_ADULT && S.form < MON_FORM_COUNT &&
        S.name[0] && memchr(S.name, 0, sizeof(S.name)) &&
        S.hunger <= 100.5f && S.happiness <= 100.5f && S.energy <= 100.5f &&
        S.cleanliness <= 100.5f && S.health <= 100.5f && S.hatch <= 100.5f;
}

bool GamePet::restoreLegacy()
{
    Preferences prefs;
    // PIXEL MON has the complete evolution state; prefer it when both exist.
    if (prefs.begin("pixmon", true)) {
        MonSave blob{};
        const size_t got = prefs.getBytes("blob", &blob, sizeof(blob));
        prefs.end();
        if (got == sizeof(blob) && blob.magic == SAVE_MAGIC) {
            S = blob.state;
            if (sane()) { Serial.println("[pet] migrated PIXEL MON save"); return true; }
        }
    }
    // Exact layout of the original CLAWD PET save. Old namespaces stay intact.
    struct OldState {
        uint32_t ageSec, lastSeenEpoch, lastSeenMs, sessionId;
        float hunger, happiness, energy, cleanliness, health, carePoints;
        uint8_t stage, flags;
        uint16_t feedCount, attentions;
        char name[12];
    };
    struct OldSave { uint32_t magic; OldState state; } blob{};
    if (!prefs.begin("clawdpet", true)) return false;
    const size_t got = prefs.getBytes("blob", &blob, sizeof(blob));
    prefs.end();
    if (got != sizeof(blob) || blob.magic != 0x50544544 || blob.state.stage > 3) return false;
    const auto &old = blob.state;
    S = {};
    S.ageSec = old.ageSec; S.lastSeenEpoch = old.lastSeenEpoch;
    S.lastSeenMs = old.lastSeenMs; S.sessionId = old.sessionId;
    S.hunger = old.hunger; S.happiness = old.happiness; S.energy = old.energy;
    S.cleanliness = old.cleanliness; S.health = old.health; S.carePoints = old.carePoints;
    S.stage = old.stage == 0 ? STAGE_BABY : (old.stage < 3 ? STAGE_CHILD : STAGE_ADULT);
    S.form = S.stage == STAGE_BABY ? FORM_PIP : (S.stage == STAGE_CHILD ? FORM_LEAF : FORM_BLOOM);
    S.flags = old.flags; S.feedCount = old.feedCount; S.attentions = old.attentions;
    memcpy(S.name, old.name, sizeof(S.name));
    S.hatch = 100; S.seen = (1u << FORM_PIP) | (1u << S.form);
    S.joy = S.vigor = 1; S.careSum = averageCare(); S.careSamples = 1;
    if (!sane()) return false;
    Serial.println("[pet] migrated original CLAWD PET save");
    return true;
}

void GamePet::save()
{
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    MonSave blob;
    blob.magic = SAVE_MAGIC;
    blob.state = S;
    prefs.putBytes("blob", &blob, sizeof(blob));
    prefs.end();
}

void GamePet::loadOutfit()
{
    outfit = 0;
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) {
        uint8_t saved = 0;
        if (prefs.getBytes("outfit", &saved, sizeof(saved)) == sizeof(saved) && saved <= outfits::count())
            outfit = saved;
        prefs.end();
    }
    previewOutfit = outfit;
}

void GamePet::wearOutfit()
{
    if (previewOutfit > outfits::count()) return;
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) { say("could not save outfit"); return; }
    const bool stored = prefs.putBytes("outfit", &previewOutfit, sizeof(previewOutfit)) == sizeof(previewOutfit);
    prefs.end();
    if (!stored) { say("could not save outfit"); return; }
    outfit = previewOutfit;
    mode = MODE_MON;
    outfitMs = 0;
    Serial.printf("[pet] outfit=%u\n", (unsigned)outfit);
}

void GamePet::openWardrobe()
{
    previewOutfit = outfit;
    outfitMs = 0;
    wardrobeDown = dragging = false;
    mode = MODE_WARDROBE;
}

void GamePet::stepOutfit(int direction)
{
    const int total = outfits::count() + 1;
    previewOutfit = (uint8_t)((previewOutfit + direction + total) % total);
    outfitMs = 0;
}

void GamePet::updateWardrobe()
{
    if (input::touchDown() && !wardrobeDown) {
        wardrobeDown = true;
        wardrobeStartX = input::tapX();
        wardrobeStartY = input::tapY();
    }
    if (!input::tapped()) {
        if (!input::touchDown()) wardrobeDown = false;
        return;
    }
    const int x = input::tapX(), y = input::tapY();
    const int dx = x - wardrobeStartX, dy = y - wardrobeStartY;
    const bool swipe = wardrobeDown && abs(dx) > 35 && abs(dx) > abs(dy) &&
        wardrobeStartY >= 115 && wardrobeStartY <= 245;
    wardrobeDown = false;
    if (swipe) { stepOutfit(dx < 0 ? 1 : -1); return; }
    if (y >= 175 && y <= 232) {
        if (x < 75) stepOutfit(-1);
        else if (x > 285) stepOutfit(1);
    } else if (y >= 261 && y <= 305) {
        if (x >= 55 && x <= 160) { mode = MODE_MON; outfitMs = 0; }
        else if (x >= 200 && x <= 305) wearOutfit();
    } else if (y >= 312 && y <= 340 && x >= 125 && x <= 235) {
        act(ACT_NAME);
        save();
    }
}

void GamePet::renderWardrobe()
{
    gfx::textCentered("WARDROBE", 32, COL_ACCENT, 2);
    char page[24];
    snprintf(page, sizeof(page), "%02u / %02u", (unsigned)previewOutfit + 1, outfits::count() + 1);
    gfx::textCentered(page, 60, COL_DIM, 1);
    const char *title = previewOutfit ? outfits::name(previewOutfit - 1) : "ORIGINAL";
    gfx::textCentered(title, 86, COL_TEXT, gfx::textWidth(title, 2) <= 270 ? 2 : 1);

    {
        const float savedX = monX, savedY = monY;
        const uint8_t savedOutfit = outfit;
        monX = 180; monY = 204; outfit = previewOutfit;
        if (S.stage == STAGE_EGG) drawEgg(); else drawMon();
        monX = savedX; monY = savedY; outfit = savedOutfit;
    }
    for (int side = 0; side < 2; ++side) {
        const float x = side ? 323 : 37;
        const float d = side ? -1 : 1;
        gfx::strokeCircle(x, 202, 20, 1, COL_RULE);
        gfx::line(x + d * 4, 194, x - d * 4, 202, COL_ACCENT);
        gfx::line(x - d * 4, 202, x + d * 4, 210, COL_ACCENT);
    }
    auto button = [](int x, const char *label, bool primary) {
        const uint16_t color = primary ? COL_ACCENT : COL_PANEL;
        gfx::fillCircle((float)x - 30, 283, 19, color);
        gfx::fillCircle((float)x + 30, 283, 19, color);
        gfx::fillRect(x - 30, 264, 60, 38, color);
        gfx::text(label, x - gfx::textWidth(label, 1) / 2, 279, primary ? COL_BG : COL_TEXT, 1);
    };
    button(108, "BACK", false); button(252, "WEAR", true);
    gfx::textCentered(S.stage == STAGE_ADULT ? "NEW EGG" : "RENAME", 324, COL_DIM, 1);
}

void GamePet::resetToEgg()
{
    // The dex is the one thing a new lifetime must not throw away: it is the reason to
    // raise a second one.
    const uint8_t dex = S.seen;
    memset(&S, 0, sizeof(S));
    S.seen        = dex;
    S.sessionId   = monSessionId;
    S.lastSeenMs  = millis();
    S.hunger      = 70.0f;
    S.happiness   = 70.0f;
    S.energy      = 80.0f;
    S.cleanliness = 90.0f;
    S.health      = 80.0f;
    S.stage       = STAGE_EGG;
    S.form        = FORM_PIP;
    strncpy(S.name, NAMES[random(0, NAME_COUNT)], sizeof(S.name) - 1);
    say("an egg appeared");
    save();
}

// -------------------------------------------------------------- simulation -----
void GamePet::decay(uint32_t sec)
{
    const float mins = sec / 60.0f;
    if (mins <= 0.0f) return;

    if (has(MF_ASLEEP)) {
        S.energy = clampf(S.energy + SLEEP_ENERGY_PER_MIN * mins, 0.0f, 100.0f);
        S.hunger = clampf(S.hunger - RATE_HUNGER * mins * SLEEP_HUNGER_SCALE, 0.0f, 100.0f);
        // Resting is one of the two ways into the vigor branch, so a mon that is put to
        // bed often grows into something different from one that is played with often.
        S.vigor += VIGOR_REST_MIN * mins;
        // It wakes itself up when it has had enough. Without this, sleeping is a state
        // the player has to remember to leave, and the screen spends the whole time
        // blocking the other five actions.
        if (S.energy >= 100.0f) {
            setFlag(MF_ASLEEP, false);
            say("i had a good nap");
            setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC);
        }
    } else {
        S.hunger      = clampf(S.hunger      - RATE_HUNGER      * mins, 0.0f, 100.0f);
        S.happiness   = clampf(S.happiness   - RATE_HAPPINESS   * mins, 0.0f, 100.0f);
        S.energy      = clampf(S.energy      - RATE_ENERGY      * mins, 0.0f, 100.0f);
        S.cleanliness = clampf(S.cleanliness - RATE_CLEANLINESS * mins, 0.0f, 100.0f);
        accrueTracks(mins);
    }

    const float drag = fminf(1.0f, HEALTH_DRAG_PER_MIN * mins);
    S.health = clampf(S.health + (averageCare() - S.health) * drag, 0.0f, 100.0f);

    const bool wasSick = has(MF_SICK);
    if (!wasSick && (S.health < SICK_AT_HEALTH || S.cleanliness < SICK_AT_CLEANLINESS)) {
        setFlag(MF_SICK, true);
        say("i feel sick...");
    } else if (wasSick && S.health > WELL_AT_HEALTH && S.cleanliness > WELL_AT_CLEANLINESS) {
        setFlag(MF_SICK, false);
        say("i feel better now");
    }

    if (averageCare() > CARE_GOOD_AVG) {
        S.carePoints += CARE_PER_MIN * mins;
    }

    S.careSum += averageCare();
    S.careSamples++;
}

void GamePet::accrueTracks(float mins)
{
    // The tracks follow the *state* of the needs, so the branch is the sum of hundreds of
    // small decisions rather than of one button pressed at the right moment.
    const float shared = 1.0f - TRACK_BASE;
    S.joy   += mins * (TRACK_BASE + shared * S.happiness / 100.0f);
    S.vigor += mins * (TRACK_BASE +
                       shared * (S.energy + S.cleanliness) * 0.5f / 100.0f);
}

void GamePet::hatchEgg()
{
    S.stage = STAGE_BABY;
    S.form  = FORM_PIP;
    S.hatch = 100.0f;
    S.careSum = 0.0f;         // the neglect check starts at the creature, not the egg
    S.careSamples = 0;
    discover(S.form);
    say("%s hatched!", S.name);
    setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC * 2.0f);
    sound::play2(660, 90, 990, 150);
    Serial.printf("[mon] hatched as %s\n", formOf(S.form).name);
    save();
}

void GamePet::evolve()
{
    const float total = S.joy + S.vigor;
    const float spread = fabsf(S.joy - S.vigor);
    const float careAvg = S.careSamples ? S.careSum / (float)S.careSamples : 100.0f;
    const bool balanced = (total > 0.0f) && (spread < BALANCE_FRAC * total + 0.5f);
    const char *why = "";

    uint8_t next;
    if (S.stage == STAGE_CHILD) {
        if (careAvg < NEGLECT_CARE_AVG) {
            next = FORM_DUSK;
            why = "neglect";
        } else if (balanced) {
            next = FORM_SOLAR;
            why = "both";
        } else if (S.form == FORM_AQUA) {
            next = FORM_TIDE;
            why = "vigor";
        } else {
            next = FORM_BLOOM;
            why = "joy";
        }
        S.stage = STAGE_ADULT;
    } else {
        if (S.joy >= S.vigor) {
            next = FORM_LEAF;
            why = "joy";
        } else {
            next = FORM_AQUA;
            why = "vigor";
        }
        S.stage = STAGE_CHILD;
    }

    S.form = next;
    S.careSum = 0.0f;             // the neglect check is about how this stage went
    S.careSamples = 0;
    discover(next);
    // The reason travels with the announcement: a tree with four endings is only
    // interesting if you can tell which branch you took and why.
    say("%s is now %s (%s)", S.name, formOf(next).name, why);
    setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC * 2.0f);
    sound::play2(520, 80, 780, 140);
    Serial.printf("[mon] evolved to %s (%s): joy=%.1f vigor=%.1f careAvg=%.1f\n",
                  formOf(next).name, why, S.joy, S.vigor, careAvg);
    save();
}

void GamePet::tick(uint32_t sec)
{
    decay(sec);

    if (S.stage == STAGE_EGG) {
        if (S.energy >= HATCH_WARM_ENERGY && S.cleanliness >= HATCH_WARM_CLEAN) {
            S.hatch += HATCH_PER_MIN * (sec / 60.0f);
            if (S.hatch >= 100.0f) {
                S.hatch = 100.0f;
                hatchEgg();
                return;
            }
        }
        return;   // an egg does not age; it is not alive yet
    }

    S.ageSec += sec;

    if (S.stage == STAGE_BABY && S.ageSec >= GATE_CHILD.ageSec && S.carePoints >= GATE_CHILD.care) {
        evolve();
    } else if (S.stage == STAGE_CHILD && S.ageSec >= GATE_ADULT.ageSec &&
               S.carePoints >= GATE_ADULT.care) {
        evolve();
    }
}

void GamePet::rollThoughts()
{
    if (S.stage == STAGE_EGG) {
        say("%s", T_EGG[random(0, 3)]);
        return;
    }
    if (has(MF_SICK))            say("%s", T_SICK[random(0, 3)]);
    else if (S.hunger < 30)      say("%s", T_HUNGRY[random(0, 3)]);
    else if (S.energy < 30)      say("%s", T_TIRED[random(0, 3)]);
    else if (S.cleanliness < 30) say("%s", T_DIRTY[random(0, 3)]);
    else if (S.happiness < 30)   say("%s", T_SAD[random(0, 3)]);
    else if (averageCare() > 75) say("%s", T_GOOD[random(0, 3)]);
    else                         say("%s", T_IDLE[random(0, 4)]);
}

void GamePet::act(int a)
{
    Serial.printf("[pet] action=%d stage=%u asleep=%d\n", a, S.stage, has(MF_ASLEEP));
    if (has(MF_ASLEEP) && a != ACT_REST && a != ACT_NAME) {
        say("zzz...");
        setAnimation(ANIM_SLEEP, 0.0f);
        return;
    }

    // An egg has no needs it can act on beyond being kept warm, so the only gestures it
    // answers are warming it in the hands and giving it a name.
    if (S.stage == STAGE_EGG) {
        if (a == ACT_PET) {
            S.hatch = clampf(S.hatch + HATCH_PER_POKE, 0.0f, 100.0f);
            jitter = 0.5f;
            say("something knocked back");
            if (S.hatch >= 100.0f) hatchEgg();
            save();
            return;
        }
        if (a != ACT_NAME) {
            say("it is still an egg");
            sound::nope();
            return;
        }
    }

    switch (a) {
    case ACT_FEED:
        S.hunger    = clampf(S.hunger + 30.0f, 0, 100);
        S.happiness = clampf(S.happiness + 10.0f, 0, 100);
        S.joy += 0.5f; S.vigor += 0.5f;
        S.feedCount++;
        say("mmm, thanks!");
        setAnimation(ANIM_EAT, ACTION_HOLD_SEC);
        break;
    case ACT_PLAY:
        S.happiness = clampf(S.happiness + 20.0f, 0, 100);
        S.energy    = clampf(S.energy    - 15.0f, 0, 100);
        S.joy += JOY_PLAY;
        S.attentions++;
        say("that was fun!");
        setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC);
        break;
    case ACT_PET:
        S.happiness = clampf(S.happiness + 8.0f, 0, 100);
        S.joy += JOY_PET;
        S.attentions++;
        say("hehe");
        setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC);
        break;
    case ACT_REST:
        setFlag(MF_ASLEEP, !has(MF_ASLEEP));
        if (has(MF_ASLEEP)) {
            say("goodnight...");
            setAnimation(ANIM_SLEEP, 0.0f);
        } else {
            say("morning!");
            setAnimation(ANIM_IDLE, 0.0f);
        }
        break;
    case ACT_SCRUB:
        startShakeGame();
        return;   // startShakeGame's caller saves through finishShakeGame
    case ACT_NAME: {
        if (S.stage == STAGE_ADULT) {
            // The last slot changes meaning once the tree is finished with.
            resetToEgg();
            say("a new egg!");
            sound::play2(780, 80, 520, 140);
            Serial.println("[mon] started a new egg; the dex was kept");
            return;
        }
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
    // Keep the launcher identity as classic orange Clawd, independent of the
    // living pet's current evolution form and its colour.
    gfx::drawSpriteSized(icon_pet, cx, cy + size * 0.06f, 0.0f, size, icon_pet_palette);
}

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

static void ellipsePanel(int cx, int cy, int rx, int ry, int tail, uint16_t fill,
                         uint16_t edge)
{
    for (int x = -rx; x <= rx; x++) {
        const float t = (float)x / (float)rx;
        const int h = (int)((float)ry * sqrtf(1.0f - t * t) + 0.5f);
        if (h <= 0) continue;
        gfx::fillRect(cx + x, cy - h, 1, h * 2, fill);
        gfx::pixel(cx + x, cy - h, edge);
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
    if (thoughtAgeSec > THOUGHT_SHOW_SEC) return;
    // Tall hats and flowers need the space above the head. Keep their feedback
    // between the body and the care buttons rather than covering the accessory.
    const int bubbleCY = outfit ? 254 : BUBBLE_CY;
    if (outfit && (dragging || fabsf(monY - PET_CY) > 6.0f)) return;

    char lines[SPEECH_MAXLINES][24];
    const int n = wrapThought(thought, lines, SPEECH_MAXLINES, SPEECH_COLS);
    if (n <= 0) return;

    int widest = 0;
    for (int i = 0; i < n; i++) {
        const int w = (int)strlen(lines[i]) * CHAR_W;
        if (w > widest) widest = w;
    }
    int rx = widest / 2 + SPEECH_PAD + 4;
    if (rx < BUBBLE_RXMIN) rx = BUBBLE_RXMIN;
    if (rx > BUBBLE_RX) rx = BUBBLE_RX;
    int ry = (n * SPEECH_LINE) / 2 + SPEECH_PAD;
    if (ry < BUBBLE_RYMIN) ry = BUBBLE_RYMIN;

    ellipsePanel(PET_CX, bubbleCY, rx, ry, outfit ? 0 : 7, COL_PANEL, gfx::dim(COL_ACCENT, 0.55f));

    const int y0 = bubbleCY - (n * SPEECH_LINE) / 2;
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
    gfx::text(line, x, y, (value < 30.0f) ? COL_WARN : COL_ACCENT, 1);
}

// A lens-shaped leaf: a run of vertical spans whose half-height follows a sine, so both
// ends come to a point. Round blobs on a stick read as eggs, which is what the first
// version of the LEAF mark was.
static void drawLeaf(int x, int y, float angle, float len, float thick, uint16_t c)
{
    const float ca = cosf(angle), sa = sinf(angle);
    // Two samples per pixel along the axis: at these angles the spine advances half a
    // pixel at a time, and one sample per pixel leaves the leaf hollow.
    const int steps = (int)(len * 2.0f);
    for (int i = 0; i <= steps; i++) {
        const float t = (float)i / (float)steps;
        const float h = sinf(PI_F * t) * thick * 0.5f;
        const float px = x + ca * len * t;
        const float py = y + sa * len * t;
        for (int k = -(int)h; k <= (int)h; k++) {
            gfx::pixel((int)(px - sa * k), (int)(py + ca * k), c);
        }
    }
}

// The thing that tells the forms apart once they share a silhouette: a mark on the head.
// Drawn rather than stored in the bitmap, because at this size a mark is three shapes and
// a bitmap is another 1.5 KB per form.
void GamePet::drawMark(int form, int cx, int cy, int scale)
{
    const mon_form_t &f = formOf(form);
    const uint16_t body = PX(f.body);
    const uint16_t edge = PX(f.edge);
    const float r = 6.0f * scale;

    switch (form) {
    case FORM_PIP:
        break;
    case FORM_LEAF: {
        // One leaf on a stem, tilted. Round blobs on a stick read as eggs, and two
        // pointed leaves at this size merged into a single blob, so it is one leaf with
        // the stem left visible under it.
        const int stemTop = cy - (int)(r * 0.85f);
        for (int y = cy; y >= stemTop; y--) {
            gfx::pixel(cx - 1 + ((cy - y) < 4 ? 0 : 1), y, edge);
        }
        drawLeaf(cx - 1, stemTop, -1.15f, r * 1.9f, r * 0.85f, body);
        break;
    }
    case FORM_AQUA:
        // A single drop, point up.
        gfx::fillCircle(cx, cy - r * 0.6f, r * 0.8f, body);
        for (int i = 0; i < (int)r; i++) {
            gfx::fillRect(cx - i * 8 / (int)r, cy - r * 1.5f + i, i * 16 / (int)r + 1, 1, body);
        }
        break;
    case FORM_BLOOM: {
        // Five petals round a centre: the leaf line flowering is the payoff for joy.
        const uint16_t petal = PX(RGB565(0xF0, 0x8A, 0xB0));
        const uint16_t core  = PX(RGB565(0xFF, 0xE0, 0x8A));
        for (int k = 0; k < 5; k++) {
            const float a = -PI_F * 0.5f + (float)k * (2.0f * PI_F / 5.0f);
            gfx::fillCircle(cx + cosf(a) * r, cy - r * 1.1f + sinf(a) * r * 0.9f,
                            r * 0.62f, petal);
        }
        gfx::fillCircle(cx, cy - r * 1.1f, r * 0.55f, core);
        break;
    }
    case FORM_TIDE:
        // A crest: the drop with a wave curled over it, so the water adult is not the
        // child's mark made bigger.
        gfx::strokeCircle(cx, cy - r * 0.9f, r * 1.1f, 2.5f, body);
        gfx::fillCircle(cx + r * 0.9f, cy - r * 1.5f, r * 0.5f, body);
        gfx::fillCircle(cx - r * 1.1f, cy - r * 0.4f, r * 0.4f, edge);
        break;
    case FORM_SOLAR: {
        // A halo and six rays: the rare form should be obvious at a glance across the room.
        const uint16_t gold = PX(RGB565(0xFF, 0xD2, 0x5A));
        gfx::strokeCircle(cx, cy - r * 1.2f, r * 1.25f, 2.0f, gold);
        for (int k = 0; k < 6; k++) {
            const float a = -PI_F * 0.5f + (float)k * (PI_F / 3.0f) * 2.0f;
            gfx::line(cx + cosf(a) * r * 1.7f, cy - r * 1.2f + sinf(a) * r * 1.7f,
                      cx + cosf(a) * r * 2.4f, cy - r * 1.2f + sinf(a) * r * 2.4f, gold);
        }
        break;
    }
    case FORM_DUSK:
        // Two dim puffs: the neglect form gets a shadow rather than a badge, which is
        // the right amount of disapproval.
        gfx::fillCircle(cx - r * 0.8f, cy - r * 0.9f, r * 0.55f, COL_DIM);
        gfx::fillCircle(cx + r * 0.7f, cy - r * 1.3f, r * 0.45f, COL_DIM);
        break;
    }
}

void GamePet::drawMon()
{
    const MoodDef &m = MOODS[anim];
    const mon_form_t &f = formOf(S.form);

    const int jx = (jitter > 0.0f) ? (int)(sinf(jitter * 45.0f) * 5.0f) : 0;
    const int bob = (m.bob > 0) ? (int)lroundf(sinf(bobPhase * 2.0f * PI_F * BOB_HZ) * m.bob) : 0;
    const int cx = (int)monX + jx;
    const int cy = (int)monY + bob;

    blitForm(f, cx, cy);

    const int lx = (int)lroundf(lookX);
    const int ly = (int)lroundf(lookY);
    const int ey = cy + f.eyeDY + ly;
    uint8_t eyes = m.eyes;
    if (anim == ANIM_IDLE && (animFrame % 4) == 3) eyes = EYE_SHUT;

    const uint16_t ink = PX(f.ink);
    drawEye(cx - f.eyeDX + lx, ey, f.eyeR, eyes, ink, +1);
    drawEye(cx + f.eyeDX + lx, ey, f.eyeR, eyes, ink, -1);
    if (m.mouth || (voice_ == Voice::Speaking && ((int)(bobPhase * 7) & 1)))
        drawMouth(cx + lx / 2, ey + f.eyeR * 2, f.eyeR, ink);

    if (outfit) outfits::draw(outfit - 1, outfitMs, cx, cy, f.w, f.h);
    else drawMark(S.form, cx, cy - f.h / 2 + 2, f.eyeR / 3 + 1);

    if (m.emote != EMOTE_NONE) {
        drawEmote(m.emote, (float)(cx + f.w / 2 - 10), (float)(cy - f.h / 2 + 6));
    }
}

void GamePet::drawEgg()
{
    const int jx = (jitter > 0.0f) ? (int)(sinf(jitter * 40.0f) * 4.0f) : 0;
    const int cx = (int)monX + jx;
    const int cy = (int)monY;

    for (int y = -EGG_RY; y <= EGG_RY; y++) {
        const float t = (float)y / (float)EGG_RY;
        float hw = EGG_RX * sqrtf(1.0f - t * t);
        if (hw <= 0.0f) continue;
        if (y < 0) hw *= 0.74f + 0.26f * (1.0f + t);   // taper: an egg, not a ball
        const int h = (int)(hw + 0.5f);
        gfx::fillRect(cx - h, cy + y, h * 2 + 1, 1, COL_SHELL);
        gfx::pixel(cx - h, cy + y, COL_SHELL_EDGE);
        gfx::pixel(cx + h, cy + y, COL_SHELL_EDGE);
    }

    // Spots in the accent, so the egg already belongs to this app's colour.
    gfx::fillCircle(cx - 13, cy + 12, 6, gfx::dim(COL_ACCENT, 0.35f));
    gfx::fillCircle(cx + 11, cy - 2, 5, gfx::dim(COL_ACCENT, 0.35f));
    gfx::fillCircle(cx + 3,  cy + 24, 4, gfx::dim(COL_ACCENT, 0.35f));

    // The shell fails in steps, which is the progress bar made physical.
    const int pct = (int)S.hatch;
    if (pct >= HATCH_CRACK1) {
        int px = cx - 22, py = cy - 6;
        for (int i = 0; i < 6; i++) {
            const int nx = px + 8, ny = py + ((i & 1) ? 5 : -5);
            gfx::line((float)px, (float)py, (float)nx, (float)ny, COL_CRACK);
            px = nx; py = ny;
        }
    }
    if (pct >= HATCH_CRACK2) {
        int px = cx - 16, py = cy + 14;
        for (int i = 0; i < 4; i++) {
            const int nx = px + 8, ny = py + ((i & 1) ? -4 : 4);
            gfx::line((float)px, (float)py, (float)nx, (float)ny, COL_CRACK);
            px = nx; py = ny;
        }
    }
    if (outfit) outfits::draw(outfit - 1, outfitMs, cx, cy, EGG_RX * 2, EGG_RY * 2);
}

bool GamePet::monHit(int x, int y) const
{
    float rx, ry;
    if (S.stage == STAGE_EGG) {
        rx = EGG_RX + 8.0f;
        ry = EGG_RY + 8.0f;
    } else {
        const mon_form_t &f = formOf(S.form);
        rx = f.w * 0.5f + 8.0f;
        ry = f.h * 0.5f + 8.0f;
    }
    const float dx = (float)x - monX;
    const float dy = (float)y - monY;
    return (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry) <= 1.0f;
}

void GamePet::drawDirt()
{
    const float rx = (S.stage == STAGE_EGG ? EGG_RX : formOf(S.form).w) * 0.30f + 14.0f;
    const float ry = (S.stage == STAGE_EGG ? EGG_RY : formOf(S.form).h) * 0.30f + 14.0f;
    for (int i = 0; i < dirt; i++) {
        const float ang = -PI_F * 0.5f + (float)i * (2.0f * PI_F / 5.0f);
        gfx::fillCircle(monX + cosf(ang) * rx, monY + sinf(ang) * ry + 4.0f, 5.0f, COL_DIM);
    }
}

void GamePet::startShakeGame()
{
    mode = MODE_SHAKE;
    dirt = 5;
    shakeIdle = 0.0f;
    shakeRefractory = 0.0f;
    shakeSampleSec = 0.0f;
    input::readRawAccel(accelPrev);
    say("shake it off!");
    setAnimation(ANIM_BATH, 0.0f);
}

void GamePet::finishShakeGame(bool completed)
{
    mode = MODE_MON;
    const float share = (5 - (float)dirt) / 5.0f;
    S.cleanliness = clampf(S.cleanliness + share * 55.0f, 0, 100);
    S.vigor += VIGOR_SCRUB * share;
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
    shakeRefractory = fmaxf(0.0f, shakeRefractory - dt);
    accelMute = fmaxf(0.0f, accelMute - dt);
    shakeIdle += dt;
    shakeSampleSec += dt;
    if (shakeIdle > 20.0f) {
        finishShakeGame(false);
        return;
    }
    // Compare three-axis samples over a fixed window, not the magnitude of
    // gravity. Left/right strokes can have identical magnitudes. The window
    // also keeps sensitivity consistent between 60Hz Arduino and 20Hz IDF IMU.
    if (shakeSampleSec < 0.05f) return;
    shakeSampleSec = 0.0f;
    float a[3];
    input::readRawAccel(a);
    float deltaSquared = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float d = a[i] - accelPrev[i];
        deltaSquared += d * d;
        accelPrev[i] = a[i];
    }

    if (accelMute <= 0.0f && shakeRefractory <= 0.0f && deltaSquared > 0.22f * 0.22f) {
        shakeRefractory = 0.18f;
        if (dirt > 0) {
            dirt--;
            Serial.printf("[mon] scrub: %u left\n", (unsigned)dirt);
            say(dirt ? "keep going!" : "done!");
            if (dirt == 0) {
                finishShakeGame(true);
                return;
            }
        }
    }
}

void GamePet::menuSlot(int i, float *sx, float *sy) const
{
    const float step = MENU_SPAN / (float)(ACT_COUNT - 1);
    const float a = (-MENU_SPAN * 0.5f + step * (float)i) * PI_F / 180.0f;
    *sx = SCR_CX + sinf(a) * MENU_R;
    *sy = SCR_CY + cosf(a) * MENU_R;
}

bool GamePet::menuEnabled(int i) const
{
    if (S.stage == STAGE_EGG) return i == ACT_PET || i == ACT_NAME;
    return !has(MF_ASLEEP) || i == ACT_REST || i == ACT_NAME;
}

int GamePet::menuHit(int x, int y) const
{
    int best = -1;
    float bestD2 = MENU_HIT * MENU_HIT;
    for (int i = 0; i < ACT_COUNT; ++i) {
        float sx, sy; menuSlot(i, &sx, &sy);
        const float dx = x - sx, dy = y - sy;
        const float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) { best = i; bestD2 = d2; }
    }
    return best >= 0 && menuEnabled(best) ? best : -1;
}

void GamePet::drawMenu()
{
    const int pressed = input::touchDown() && !dragging ? menuHit(input::tapX(), input::tapY()) : -1;
    for (int i = 0; i < ACT_COUNT; i++) {
        float sx, sy;
        menuSlot(i, &sx, &sy);
        const char *label = actLabelFor(i, S.stage);
        const int w = gfx::textWidth(label, 1);

        gfx::strokeCircle(sx, sy, MENU_HIT * 0.82f, 1.0f,
                          gfx::dim(COL_ACCENT, menuEnabled(i) ? 0.45f : 0.15f));
        const bool active = i == pressed || i == lastTapped;
        if (active) {
            gfx::fillCircle(sx, sy, MENU_HIT * 0.82f, gfx::dim(COL_ACCENT, 0.55f));
        }
        gfx::text(label, (int)(sx - w * 0.5f), (int)(sy - 4.0f),
                  menuEnabled(i) ? (active ? COL_TEXT : COL_ACCENT) : COL_DIM, 1);
    }
}

// -------------------------------------------------------------------- frame ----
void GamePet::enter()
{
#ifdef ARDUINO_ARCH_ESP32
    // The launcher stays lightweight. If the integrated voice-pet image is installed,
    // this one entry enters it; talking and caring then run together without rebooting.
    const auto *voice = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
    esp_app_desc_t desc{};
    if (voice && esp_ota_get_partition_description(voice, &desc) == ESP_OK &&
        !strcmp(desc.project_name, "clawd-pet") && esp_ota_set_boot_partition(voice) == ESP_OK) {
        ESP.restart();
    }
#endif
    if (!monSessionId) monSessionId = (uint32_t)esp_random();

    const uint32_t nowMs = millis();
    load();
    loadOutfit();
    mode = MODE_MON;
    wardrobeDown = dragging = false;
    outfitMs = 0;

    uint32_t awaySec = 0;
    if (sysclock::valid() && S.lastSeenEpoch > 1700000000u) {
        const uint32_t nowEpoch = (uint32_t)time(nullptr);
        if (nowEpoch > S.lastSeenEpoch) awaySec = nowEpoch - S.lastSeenEpoch;
    } else if (S.sessionId == monSessionId && nowMs > S.lastSeenMs) {
        awaySec = (nowMs - S.lastSeenMs) / 1000;
        if (awaySec > AWAY_CAP_SEC) awaySec = AWAY_CAP_SEC;
    }
    if (awaySec > 0) {
        Serial.printf("[mon] %u s passed off-screen; applying\n", (unsigned)awaySec);
        tick(awaySec);
        save();
    }
    S.sessionId = monSessionId;
    S.lastSeenMs = nowMs;
    S.lastSeenEpoch = sysclock::valid() ? (uint32_t)time(nullptr) : 0;

    anim = has(MF_ASLEEP) ? ANIM_SLEEP : (has(MF_SICK) ? ANIM_SICK : ANIM_IDLE);
    animFrame = 0;
    animFrameSec = 0.0f;
    animHoldSec = 0.0f;
    tickAccum = 0.0f;
    saveCounter = 0.0f;
    thoughtTimer = 0.0f;
    lastTapped = -1;
    tapFlashSec = 0.0f;
    bobPhase = 0.0f;
    monX = SCR_CX;
    monY = 176.0f;
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
    // Keep the accessory's small movement on its own bounded clock.
    const uint8_t shown = wardrobeActive() ? previewOutfit : outfit;
    const uint32_t loop = shown ? outfits::duration(shown - 1) : 0;
    outfitMs = loop ? (outfitMs + ms) % loop : 0;
    S.lastSeenMs = millis();
    if (sysclock::valid()) S.lastSeenEpoch = (uint32_t)time(nullptr);

    tickAccum += dt;
    const uint32_t wholeSec = (uint32_t)tickAccum;
    if (wholeSec > 0) {
        tickAccum -= (float)wholeSec;
        tick(wholeSec);
    }

    animFrameSec += dt;
    if (animFrameSec >= FRAME_SEC) {
        animFrameSec = 0.0f;
        animFrame++;
    }
    if (animHoldSec > 0.0f) {
        animHoldSec -= dt;
    } else {
        const int rest = has(MF_ASLEEP) ? ANIM_SLEEP : (has(MF_SICK) ? ANIM_SICK : ANIM_IDLE);
        if (anim != rest) setAnimation(rest, 0.0f);
    }

    thoughtAgeSec += dt;
    thoughtTimer += ms;
    if (thoughtTimer >= THOUGHT_EVERY_MS) {
        thoughtTimer = 0;
        rollThoughts();
    }

    tapFlashSec = fmaxf(0.0f, tapFlashSec - dt);
    if (tapFlashSec <= 0.0f) lastTapped = -1;

    if (input::touchDown() && !dragging) {
        lookX = clampf((input::tapX() - monX) * 0.12f, -5.0f, 5.0f);
        lookY = clampf((input::tapY() - monY) * 0.12f, -5.0f, 5.0f);
    } else {
        lookX *= 1.0f - fminf(1.0f, 4.0f * dt);
        lookY *= 1.0f - fminf(1.0f, 4.0f * dt);
    }

    if (voiceActive()) {
        dragging = wardrobeDown = false;
    } else if (mode == MODE_WARDROBE) {
        updateWardrobe();
    } else if (mode == MODE_SHAKE) {
        if (input::tapped()) {
            finishShakeGame(dirt == 0);
        } else {
            updateShakeGame(dt);
        }
    } else {
        if (input::touchDown() && monHit(input::tapX(), input::tapY())
            && input::touchHeldMs() > 200) {
            dragging = true;
        }
        if (dragging) {
            monX = clampf((float)input::tapX(), 84.0f, 276.0f);
            monY = clampf((float)input::tapY(), 126.0f, 240.0f);
        } else {
            monX += ((float)PET_CX - monX) * fminf(1.0f, 2.5f * dt);
            monY += ((float)PET_CY - monY) * fminf(1.0f, 2.5f * dt);
        }

        if (input::tapped()) {
            accelMute = 0.25f;
            if (dragging) {
                dragging = false;
            } else if (monHit(input::tapX(), input::tapY())) {
                handleMonTouch();
            } else {
                handleMenuTouch();
            }
        }
    }

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

void GamePet::handleMonTouch()
{
    pokeHeat += 1.0f;
    pokeCool = 1.2f;

    if (S.stage == STAGE_EGG) {
        // Warming it in the hands is the egg's only game, and it is also the tutorial for
        // touching the creature that comes out of it.
        act(ACT_PET);
        return;
    }

    if (pokeHeat >= 3.0f) {
        S.happiness = clampf(S.happiness - 2.0f, 0, 100);
        say("hey, that is enough");
        setAnimation(ANIM_ANNOYED, ACTION_HOLD_SEC);
        jitter = 0.6f;
    } else {
        S.happiness = clampf(S.happiness + 6.0f, 0, 100);
        S.joy += 0.5f;
        S.attentions++;
        say("hehe");
        setAnimation(ANIM_HAPPY, ACTION_HOLD_SEC);
    }
    save();
}

void GamePet::handleMenuTouch()
{
    if (voiceActive()) return;
    const int best = menuHit(input::tapX(), input::tapY());
    if (best >= 0) {
        lastTapped = best;
        tapFlashSec = 0.15f;
        if (best == ACT_NAME) openWardrobe();
        else act(best);
    }
}

void GamePet::render()
{
    gfx::clear(COL_BG);

    if (voiceActive()) {
        gfx::textCentered("CLAWD PET", 28, COL_ACCENT, 2);
        const char *label = voice_ == Voice::Listening ? "LISTENING" :
            (voice_ == Voice::Thinking ? "THINKING" : "SPEAKING");
        gfx::textCentered(label, 67, COL_DIM, 1);
        if (S.stage == STAGE_EGG) drawEgg(); else drawMon();
        const float t = bobPhase * 6;
        for (int i = -2; i <= 2; ++i) {
            const int h = 3 + (int)(fabsf(sinf(t + i * 0.8f)) * 9);
            gfx::fillRect(178 + i * 9, 225 - h / 2, 4, h, COL_ACCENT);
        }
        setDirty(0, SCR_H - 1);
        return;
    }

    if (mode == MODE_WARDROBE) {
        renderWardrobe();
        setDirty(0, SCR_H - 1);
        return;
    }

    const char *stage = (S.stage == STAGE_EGG) ? "EGG" : formOf(S.form).stage;
    char head[48];
    snprintf(head, sizeof(head), "%.8s | %s | %s", S.name, stage, sysclock::timeText());
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
        // The egg's third row is its progress; after hatching it is health, where it was.
        const char *label = (S.stage == STAGE_EGG) ? "WARM" : "HEALTH";
        const float value = (S.stage == STAGE_EGG) ? S.hatch : S.health;
        char line[32];
        const int filled = (int)lroundf(clampf(value, 0, 100) / 100.0f * BAR_CHARS);
        int k = snprintf(line, sizeof(line), "%-6s [", label);
        for (int i = 0; i < BAR_CHARS; i++) line[k++] = (i < filled) ? '#' : '.';
        line[k++] = ']';
        line[k] = 0;
        const int w = gfx::textWidth(line, 1);
        gfx::text(line, (int)(SCR_CX - w * 0.5f), HEALTH_Y, COL_TEXT, 1);
    }

    if (S.stage == STAGE_EGG) drawEgg();
    else drawMon();
    drawThought();
    drawMenu();

    {
        // The bottom line is the only place that can say why the mon has not evolved yet.
        // Both conditions have to hold, so it names whichever one is actually the
        // bottleneck rather than showing two numbers nobody can read at a glance.
        char line[28];
        int k;
        if (S.stage == STAGE_EGG) {
            k = snprintf(line, sizeof(line), "EGG %d%%  ", (int)S.hatch);
        } else if (S.stage == STAGE_ADULT) {
            k = snprintf(line, sizeof(line), "MAX  ");
        } else {
            const Gate g = (S.stage == STAGE_BABY) ? GATE_CHILD : GATE_ADULT;
            if (S.carePoints < g.care) {
                k = snprintf(line, sizeof(line), "GROW %d/%d  ",
                             (int)S.carePoints, (int)g.care);
            } else {
                const uint32_t left = (g.ageSec > S.ageSec) ? (g.ageSec - S.ageSec) : 0;
                k = snprintf(line, sizeof(line), "AGE %lum  ",
                             (unsigned long)((left + 59) / 60));
            }
        }
        snprintf(line + k, sizeof(line) - k, "SEEN %d/7", __builtin_popcount(S.seen));
        gfx::textCentered(line, DEX_Y, COL_DIM, 1);
    }
    setDirty(0, SCR_H - 1);
}

void GamePet::renderShakeGame()
{
    char line[32];
    snprintf(line, sizeof(line), "SHAKE  %u LEFT", (unsigned)dirt);
    gfx::textCentered(line, METER_ROW0, dirt ? COL_ACCENT : COL_TEXT, 1);
    gfx::textCentered("hold the badge and shake it", METER_ROW1 + 4, COL_DIM, 1);

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
    if (S.stage == STAGE_EGG) drawEgg();
    else drawMon();
    drawThought();
    gfx::textCentered("tap to stop", SCR_H - 22, COL_DIM, 1);
}
