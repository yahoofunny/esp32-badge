#pragma once

#include "game.h"

// clawd's pet, in the idiom of claude-code-tamagotchi.
//
// That project's model is adopted here because it is better than the blog version
// this app started as:
//
//   * four needs -- hunger, happiness, energy, cleanliness -- that all decay and all
//     read "fuller is better". The blog version tracked hunger as a need that
//     *rises*, so a full bar meant a starving pet and an empty one meant a fed pet.
//   * a derived health value that lags the average of the four, so neglect surfaces
//     as illness rather than as a fifth bar to keep topped up.
//   * sickness that both starts and ends by itself, from health and cleanliness.
//   * growth driven by accumulated care rather than by elapsed time alone.
//   * the pet is a face that emotes, not a bitmap creature.
//
// One substitution: the original draws its face with kaomoji such as (◕ᴥ◕), and the
// system font here is 8x8 ASCII only. The equivalent ASCII emoticons -- (^_^),
// \(^o^)/ -- are what a terminal would have used anyway, and they render exactly.
// Defined at file scope so the static tables in the implementation can size
// themselves with them; private class enums are not visible there.
enum PetAction { ACT_FEED, ACT_PLAY, ACT_PET, ACT_SLEEP, ACT_BATHE, ACT_NAME, ACT_COUNT };
enum PetAnim   { ANIM_IDLE, ANIM_HAPPY, ANIM_EAT, ANIM_SLEEP, ANIM_BATH, ANIM_SICK,
                 ANIM_COUNT };

class GamePet : public Game {
public:
    const char *name() const override { return "CLAWD PET"; }
    uint16_t    accent() const override { return PX(RGB565(0xD9, 0x77, 0x57)); }
    void drawIcon(float cx, float cy, float size) const override;

    // Exercises the save/load round-trip. Navigating the app is the only other way
    // to test persistence, and reaching a state worth checking takes minutes.
    bool selfTest();

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;

private:
    struct State {
        // Time the pet spent off-screen is accounted for, so being away does not
        // freeze it. millis() restarts at zero each power-on, so a stored timestamp is
        // only meaningful within one boot; sessionId identifies the boot and the gap
        // is applied only while it still matches.
        uint32_t ageSec;
        uint32_t lastSeenMs;
        uint32_t sessionId;

        float    hunger;        // all four are 0..100, higher is better
        float    happiness;
        float    energy;
        float    cleanliness;
        float    health;        // derived: drags towards the average of the four
        float    carePoints;    // accumulates while the pet is well kept
        uint8_t  stage;         // 0 baby, 1 child, 2 teen, 3 adult
        uint8_t  flags;
        uint16_t feedCount;
        uint16_t attentions;    // plays and pets together
        char     name[12];
    };

    enum {
        PF_ASLEEP = 1 << 0,
        PF_SICK   = 1 << 1,
    };

    struct PetSave {
        uint32_t magic;
        State    state;
    };

    State S;

    // The face. An animation is a short loop of emoticon frames; an action switches to
    // its own for a moment and the pet then drifts back to idle.
    uint8_t anim      = ANIM_IDLE;
    uint8_t animFrame = 0;
    float   animFrameSec = 0.0f;
    float   animHoldSec  = 0.0f;   // seconds left before reverting to idle

    char  thought[42] = {0};
    float thoughtAgeSec = 0.0f;

    float tickAccum    = 0.0f;
    float saveCounter  = 0.0f;
    float thoughtTimer = 0.0f;
    int   lastTapped   = -1;

    // --- simulation ---------------------------------------------------------
    bool  has(uint8_t f) const { return (S.flags & f) != 0; }
    void  setFlag(uint8_t f, bool on);
    void  resetToNewPet();
    void  tick(uint32_t sec);
    void  decay(uint32_t sec);
    void  rollThoughts(uint32_t sec);
    void  act(int action);
    void  say(const char *fmt, ...);
    void  setAnimation(int a, float holdSec);
    float averageCare() const;

    // --- persistence --------------------------------------------------------
    void load();
    void save();

    // --- presentation -------------------------------------------------------
    void drawMeter(const char *label, float value, int x, int y);
    void drawFace();
    void drawMenu();
    void layoutMenu(float *xs, float *ys, float *ws, bool *enabled, int *count) const;
};
