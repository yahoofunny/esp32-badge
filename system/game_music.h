#pragma once
#include "game.h"
class Audio;
struct MusicPump;

class GameMusic : public Game {
public:
    const char *name() const override {return "MUSIC";}
    uint16_t accent() const override {return PX(RGB565(0xF0,0x9A,0x3C));}
    void drawIcon(float cx,float cy,float size) const override;
    void enter() override;
    void leave() override;
    void update(float dt) override;
    void render() override;
    void nextTrack(int direction=1);
    void togglePlayback();
    void deleteCurrentTrack();   // removes the loaded file, two taps away
    void printDiagnostics() const;
    bool setVolumePercent(int percent,bool remember=true);
private:
    friend struct MusicTestAccess;
    Audio *player=nullptr;
    MusicPump *pump=nullptr;
    int index=0,tracks=0,pressed=-1,startTarget=-1,startX=0,startY=0,volume=0;
    bool paused=false,failed=false,tracking=false,moved=false,redraw=true,painted=false;
    uint32_t elapsed=0,duration=0,lastFrame=0;
    float rotation=0;
    char title[28]={0};
    void playCurrent();
    static void pumpAudio(void *context);
};
