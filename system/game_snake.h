#pragma once
#include "game.h"

class GameSnake : public Game {
public:
    const char *name() const override {return "SNAKE";}
    uint16_t accent() const override {return PX(RGB565(0x80,0xCF,0x9B));}
    void drawIcon(float cx,float cy,float size) const override;
    void enter() override;
    void update(float dt) override;
    void render() override;
    void printDiagnostics() const;
private:
    friend struct MiniGamesTestAccess;
    enum Mode {READY,PLAYING,PAUSED,LOST,WON};
    static constexpr int SIDE=15,CAPACITY=SIDE*SIDE;
    uint16_t body[CAPACITY]={};
    int length=4,food=0,score=0,best=0,direction=1,turns[2]={},turnCount=0;
    Mode mode=READY;
    float elapsed=0;
    bool redraw=true,tracking=false,moved=false,pauseTouch=false,pressed=false;
    int startX=0,startY=0;
    void reset();
    void placeFood();
    void queueTurn(int next);
    void step();
};
