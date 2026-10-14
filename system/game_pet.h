#pragma once

#include "assets_pet.h"
#include "game.h"

// clawd 电子宠物 — the Tamagotchi from the blog, ported to the badge.
//
// The original is a single-page web toy whose data model is: four needs that drift
// over time, a life stage derived from age, growth gated on how well it has been
// cared for, sickness when several needs go critical at once, and a pet that leaves
// if neglected for long enough. All of that is reproduced here with the same numbers
// and the same thresholds, so the two behave alike.
//
// The port keeps the original's two deliberate cruelties: growth can be delayed by
// poor care (though adulthood only waits for time), and neglect is permanent — a pet
// that runs away leaves an empty screen until you adopt a new egg.
class GamePet : public Game {
public:
    const char *name() const override { return "CLAWD PET"; }
    uint16_t    accent() const override { return PX(RGB565(0xD7, 0x77, 0x57)); }
    void drawIcon(float cx, float cy, float size) const override;

    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;

private:
    // --- data model ---------------------------------------------------------
    // Needs are 0..100. Hunger and boredom rise; cleanliness and energy fall. A
    // need at the "bad" end of its range is what the checks below look at.
    struct State {
        uint32_t ageSec;            // accumulated powered-on time
        float    hunger;            // 0 good .. 100 starving
        float    fun;               // 0 good .. 100 bored
        float    clean;             // 100 good .. 0 filthy
        float    energy;            // 100 good .. 0 exhausted
        uint8_t  stage;             // 0 egg, 1 baby, 2 teen, 3 adult
        uint8_t  flags;             // bitfield, see PF_*
        uint32_t hungerPeakSec;     // when hunger last hit 100, 0 if it has not
        uint32_t criticalSinceSec;  // when the multi-need critical state began

        // Time the pet spent off-screen is accounted for, so being away does not
        // freeze it -- the point of a virtual pet is that it keeps living while you
        // do something else.
        //
        // millis() restarts at zero on every power-on, so a saved timestamp is only
        // meaningful within the same boot. sessionId identifies the boot, and the
        // away time is only applied when it still matches.
        uint32_t sessionId;
        uint32_t lastSeenMs;
    };

    enum {
        PF_POOP     = 1 << 0,
        PF_SLEEPING = 1 << 1,
        PF_SICK     = 1 << 2,
        PF_RUNAWAY  = 1 << 3,
    };

    State S;

    // --- derived ------------------------------------------------------------
    float    ageMin() const { return S.ageSec / 60.0f; }
    int      stageForAge() const;
    float    careScore() const;
    bool     has(uint8_t f) const { return (S.flags & f) != 0; }
    void     setFlag(uint8_t f, bool on);
    const pet_sprite_t &sprite() const;

    // --- simulation ---------------------------------------------------------
    void resetToNewEgg();
    void decay(uint32_t sec);
    void tick(uint32_t sec);
    void checkGrow();
    void checkSick();
    void checkRunaway();
    void checkPoop(uint32_t sec);
    void doRunaway(const char *why);
    void act(int action);
    void say(const char *fmt, ...);

    // --- persistence --------------------------------------------------------
    void load();
    void save();

    // --- presentation -------------------------------------------------------
    void drawBars();
    void drawButtons();
    void layoutButtons(float *xs, float *ys, float *radii, bool *enabled, int *count) const;
    void drawPetSprite();

    char     msg[40] = {0};
    uint32_t msgAgeMs = 0;
    float    bobPhase = 0.0f;
    // Whole-second accumulator: the simulation rates are per minute, but update()
    // arrives at ~60 Hz, so the sub-second remainder has to be carried or the pet
    // would never age at all.
    float    tickAccum = 0.0f;
    float    saveCounter = 0.0f;

    // Index of the button currently under the finger, for press feedback.
    int pressedButton = -1;
    int lastTapped = -1;
};
