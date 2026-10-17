#pragma once
#include "game.h"

class GameHypno : public Game {
public:
    const char *name() const override { return "HYPNO"; }
    uint16_t accent() const override { return PX(RGB565(0xBD,0x92,0xEC)); }
    void drawIcon(float cx,float cy,float size) const override;
    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;
private:
    friend struct HypnoTestAccess;
    float phase=0,halo=0;
    int speed=1,pressed=-1,startTarget=-1,startX=0,startY=0;
    bool paused=false,tracking=false,moved=false,redraw=true,painted=false;
    uint32_t lastFrame=0;
};
