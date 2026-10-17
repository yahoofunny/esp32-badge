#pragma once
#include "game.h"

class GameBricks : public Game {
public:
    const char *name() const override {return "BRICKS";}
    uint16_t accent() const override {return PX(RGB565(0xEF,0xAB,0x76));}
    void drawIcon(float cx,float cy,float size) const override;
    void enter() override;
    void update(float dt) override;
    void render() override;
    void printDiagnostics() const;
private:
    friend struct MiniGamesTestAccess;
    enum Mode {READY,SERVING,PLAYING,PAUSED,LOST,WON};
    Mode mode=READY,resumeMode=PLAYING;
    bool bricks[20]={};
    float ballX=180,ballY=274,vx=78,vy=-140,paddle=180,held=0;
    int score=0,best=0,lives=3,remaining=20;
    bool redraw=true,painted=false,tracking=false,moved=false;
    int startX=0,startY=0,startTarget=-1,pressed=-1;
    uint32_t lastFrame=0;
    void reset();
    void serve();
    void movePaddle(float delta);
    void advance(float dt);
};
