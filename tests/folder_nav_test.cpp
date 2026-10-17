// Actual system.ino registries, navigation helpers and BOOT state machine.
#include <Arduino.h>
#include <cassert>
#include <vector>
#include "../system/launcher.cpp"

namespace {
bool finger=false,released=false,boot=false;
unsigned long held=0;
int voiceSwitches=0;
struct App:Game {
    int enters=0,leaves=0;
    const char *name()const override{return "APP";}
    uint16_t accent()const override{return 0xffff;}
    void enter()override{++enters;} void leave()override{++leaves;}
    void update(float)override{} void render()override{}
} g_clawd,g_tap,g_pet,g_photos,g_music,g_hypno,g_snake,g_bricks,g_settings;
GameFolder g_folder;GameBack g_back;
}
namespace input {
bool touchDown(){return finger;}bool tapped(){return released;}
int tapX(){return 180;}int tapY(){return 180;}float tiltX(){return 0;}
bool bootDown(){return boot;}unsigned long bootHeldMs(){return held;}
}
namespace sound {void play2(int,int,int,int){}void launch(){}}
namespace xiaozhi {void switchToAssistant(){++voiceSwitches;}}
#include "../build/folder_nav_functions.h"

static void tapSelected() {
    released=true;assert(!launcher::update(.016f));released=false;
    Game *picked=nullptr;
    for(int i=0;i<40&&!picked;++i)picked=launcher::update(.016f);
    assert(picked);openPicked(picked);
}
static void releaseBoot(){boot=false;held=0;handleBootButton();}
static void holdBoot(unsigned long ms){boot=true;held=ms;handleBootButton();}

int main() {
    std::vector<uint16_t> pixels(SCR_W*SCR_H);fb=pixels.data();
    assert(GAME_COUNT==6&&MINI_GAME_COUNT==5);
    assert(GAMES[4]==&g_folder&&MINI_GAMES[4]==&g_back);
    showLauncher(false);
    launcher::begin(GAMES,GAME_COUNT,4);tapSelected();
    assert(inGameFolder&&screen==SCREEN_LAUNCHER&&!active&&rootSelection==4);
    assert(launcher::g_games==MINI_GAMES&&launcher::g_count==5);
    launcher::render();
    launcher::begin(MINI_GAMES,MINI_GAME_COUNT,2);tapSelected();
    assert(inGameFolder&&active==&g_snake&&g_snake.enters==1&&folderSelection==2);
    holdBoot(899);assert(screen==SCREEN_GAME);
    holdBoot(900);
    assert(screen==SCREEN_LAUNCHER&&inGameFolder&&!active&&g_snake.leaves==1);
    assert(launcher::selectedIndex()==2);
    holdBoot(5000);assert(inGameFolder&&!voiceSwitches); // One hold: one level.
    releaseBoot();holdBoot(900);
    assert(!inGameFolder&&screen==SCREEN_LAUNCHER&&launcher::selectedIndex()==4);
    holdBoot(5000);assert(!voiceSwitches);releaseBoot();
    tapSelected();assert(inGameFolder&&launcher::selectedIndex()==2);
    launcher::begin(MINI_GAMES,MINI_GAME_COUNT,4);tapSelected(); // BACK tile.
    assert(!inGameFolder&&!active&&launcher::selectedIndex()==4&&folderSelection==4);
    // All four games route back to their parent, without moving main apps there.
    for(int i=0;i<4;++i) {
        showLauncher(true);launcher::begin(MINI_GAMES,MINI_GAME_COUNT,i);tapSelected();
        assert(screen==SCREEN_GAME&&active==MINI_GAMES[i]);
        releaseBoot();holdBoot(900);
        assert(inGameFolder&&screen==SCREEN_LAUNCHER&&launcher::selectedIndex()==i);
        releaseBoot();
    }
    showLauncher(false);launcher::begin(GAMES,GAME_COUNT,1);tapSelected();
    assert(active==&g_photos&&!inGameFolder);
    holdBoot(900);assert(!inGameFolder&&screen==SCREEN_LAUNCHER&&launcher::selectedIndex()==1);
    releaseBoot();holdBoot(2199);assert(!voiceSwitches);
    holdBoot(2200);assert(voiceSwitches==1);holdBoot(5000);assert(voiceSwitches==1);
    puts("PASS: actual registries and folder launch, BACK, remembered selections, parent BOOT return, held-button latch, home voice shortcut");
}
