#pragma once

#include "game.h"
#include <stddef.h>

// A pocket mon: an egg, a creature that hatches from it, and a branching evolution
// tree that decides what it grows into.
//
// The care model is the pet's -- four needs that all decay, a derived health, sickness
// that begins and ends on its own -- because that part is already right, and a Tamagotchi
// is a Tamagotchi. What is different is what the care is *for*:
//
//   * the first screen is an egg. It has to be kept warm to hatch, so the needs are
//     load-bearing before there is anything to look after.
//   * two tracks, joy and vigor, quietly accumulate from the state of the needs and from
//     which actions are used. Which one leads is what picks the child form, and the ratio
//     between them plus the overall care picks the adult one -- so the same egg raised two
//     ways gives two different creatures, and the dex fills up over several lifetimes.
//   * the body is clawd's silhouette at one size per stage with the form's colour, and the
//     forms are told apart by the mark drawn on top of it. See tools/make_mon_sprites.py.
//
// Namespaced so action and animation names do not leak into other games.
// This model is shared by the Arduino launcher and the ESP-IDF voice companion.
namespace mon {
enum MonForm { FORM_PIP, FORM_LEAF, FORM_AQUA, FORM_BLOOM, FORM_TIDE, FORM_SOLAR,
               FORM_DUSK, FORM_COUNT };
enum MonStage { STAGE_EGG, STAGE_BABY, STAGE_CHILD, STAGE_ADULT };
enum MonAction { ACT_FEED, ACT_PLAY, ACT_PET, ACT_REST, ACT_SCRUB, ACT_NAME, ACT_COUNT };
enum MonAnim   { ANIM_IDLE, ANIM_HAPPY, ANIM_EAT, ANIM_SLEEP, ANIM_BATH, ANIM_SICK,
                 ANIM_ANNOYED, ANIM_COUNT };
}  // namespace mon

class GamePet : public Game {
public:
    const char *name() const override { return "CLAWD PET"; }
    uint16_t    accent() const override { return PX(RGB565(0xD9, 0x77, 0x57)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;

    enum class Voice { Idle, Listening, Thinking, Speaking };
    // UI-thread only. Voice changes presentation, never the saved needs.
    void setVoice(Voice voice) { voice_ = voice; }
    void react(const char *emotion);
    bool voiceActive() const { return voice_ != Voice::Idle; }
    bool wardrobeActive() const { return mode == MODE_WARDROBE && !voiceActive(); }
    void describe(char *out, size_t size) const;

private:
    struct State {
        uint32_t ageSec;
        uint32_t lastSeenEpoch;   // unix seconds, from the RTC
        uint32_t lastSeenMs;      // the same-boot fallback
        uint32_t sessionId;

        float    hunger;
        float    happiness;
        float    energy;
        float    cleanliness;
        float    health;          // derived: drags towards the average of the four
        float    carePoints;      // accumulates while the mon is well kept
        float    joy;             // the two branch tracks; which leads decides the form
        float    vigor;
        float    careSum;         // running average of the needs, for the neglect branch
        uint16_t careSamples;

        uint8_t  stage;           // MonStage
        uint8_t  form;            // index into mon_forms[], meaningful from STAGE_BABY up
        uint8_t  flags;
        uint8_t  seen;            // dex: one bit per form, kept across lifetimes
        float    hatch;           // 0..100, the egg's progress
        uint16_t feedCount;
        uint16_t attentions;
        char     name[12];
    };

    enum {
        MF_ASLEEP = 1 << 0,
        MF_SICK   = 1 << 1,
    };

    struct MonSave {
        uint32_t magic;
        State    state;
    };

    State S{};
    Voice voice_ = Voice::Idle;
    bool restoreLegacy();
    bool sane() const;

    enum { MODE_MON, MODE_SHAKE, MODE_WARDROBE };
    uint8_t mode = MODE_MON;
    // Stored separately from the original care blob, preserving both old layouts.
    uint8_t outfit = 0, previewOutfit = 0; // 0 = evolution mark; others add accessories
    uint32_t outfitMs = 0;
    bool wardrobeDown = false;
    int wardrobeStartX = 0, wardrobeStartY = 0;
    void loadOutfit();
    void wearOutfit();
    void openWardrobe();
    void stepOutfit(int direction);
    void updateWardrobe();
    void renderWardrobe();

    // Direct manipulation, as with the pet: it is draggable, and it looks at the finger.
    float monX = SCR_CX, monY = 176.0f;
    float lookX = 0.0f, lookY = 0.0f;
    bool  dragging = false;
    float pokeHeat = 0.0f;
    float pokeCool = 0.0f;
    float jitter = 0.0f;

    // Shake-off-the-dirt.
    uint8_t dirt = 0;
    float   shakeIdle = 0.0f;
    float   shakeRefractory = 0.0f;
    float   accelPrevMag = 1.0f;
    float   accelMute = 0.0f;

    uint8_t anim      = mon::ANIM_IDLE;
    uint8_t animFrame = 0;
    float   animFrameSec = 0.0f;
    float   animHoldSec  = 0.0f;
    float   bobPhase     = 0.0f;

    char  thought[42] = {0};
    float thoughtAgeSec = 0.0f;

    float tickAccum    = 0.0f;
    float saveCounter  = 0.0f;
    float thoughtTimer = 0.0f;
    int   lastTapped   = -1;
    float tapFlashSec = 0.0f;

    // --- simulation ---------------------------------------------------------
    bool  has(uint8_t f) const { return (S.flags & f) != 0; }
    void  setFlag(uint8_t f, bool on);
    void  resetToEgg();
    void  tick(uint32_t sec);
    void  decay(uint32_t sec);
    void  accrueTracks(float mins);
    void  rollThoughts();
    void  act(int action);
    void  say(const char *fmt, ...);
    void  setAnimation(int a, float holdSec);
    float averageCare() const;
    void  discover(int form);
    void  hatchEgg();
    void  evolve();

    // --- persistence --------------------------------------------------------
    void load();
    void save();

    // --- presentation -------------------------------------------------------
    void drawMeter(const char *label, float value, int x, int y);
    void drawMon();
    void drawEgg();
    void drawMark(int form, int cx, int cy, int scale);
    void drawDirt();
    void drawMenu();
    void drawThought();
    void menuSlot(int i, float *sx, float *sy) const;
    bool menuEnabled(int i) const;
    int menuHit(int x, int y) const;

    // --- interaction --------------------------------------------------------
    void handleMonTouch();
    void handleMenuTouch();
    void startShakeGame();
    void finishShakeGame(bool completed);
    void updateShakeGame(float dt);
    void renderShakeGame();
    bool monHit(int x, int y) const;
};
