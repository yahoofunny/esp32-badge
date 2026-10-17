#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <Arduino.h>
#include "../system/game_snake.h"
#include "../system/game_bricks.h"

namespace {bool finger=false,released=false;int px=180,py=180;}
namespace input {
bool touchDown(){return finger;}bool tapped(){return released;}
int tapX(){return px;}int tapY(){return py;}
}
struct MiniGamesTestAccess {
    static void snakeChecks(GameSnake &g){
        assert(g.mode==GameSnake::READY&&g.length==4);
        g.mode=GameSnake::PLAYING;g.queueTurn(3);assert(!g.turnCount);
        g.queueTurn(0);g.queueTurn(3);g.queueTurn(2);assert(g.turnCount==2);
        g.step();assert(g.direction==0&&g.body[0]==97);
        g.step();assert(g.direction==3&&g.body[0]==96);
        g.reset();g.mode=GameSnake::PLAYING;g.food=g.body[0]+1;g.step();
        assert(g.length==5&&g.score==1&&g.best==1);
        for(int i=0;i<g.length;++i)assert(g.food!=g.body[i]);
        // Moving into the just-departing tail is legal without growth.
        g.length=4;g.body[0]=80;g.body[1]=95;g.body[2]=94;g.body[3]=79;
        g.direction=3;g.food=0;g.turnCount=0;g.step();
        assert(g.mode==GameSnake::PLAYING&&g.body[0]==79);
        g.body[0]=0;g.direction=0;g.step();assert(g.mode==GameSnake::LOST);
        g.reset();g.mode=GameSnake::PLAYING;g.length=6;
        const int loop[]={80,81,96,95,94,79};for(int i=0;i<6;++i)g.body[i]=loop[i];
        g.direction=2;g.step();assert(g.mode==GameSnake::LOST);
        // Last free cell: no rejection loop, no capacity overrun, a real win.
        g.reset();g.mode=GameSnake::PLAYING;g.length=224;g.direction=1;g.food=224;
        for(int i=0;i<224;++i)g.body[i]=223-i;
        g.step();assert(g.mode==GameSnake::WON&&g.length==225&&g.food==-1);
    }
    static void bricksChecks(GameBricks &g){
        assert(g.mode==GameBricks::READY);g.mode=GameBricks::PLAYING;
        g.ballX=71.1f;g.ballY=200;g.vx=-160;g.vy=0;g.advance(.02f);
        assert(g.ballX>=71&&g.vx>0);
        g.ballX=288.9f;g.vx=160;g.advance(.02f);assert(g.ballX<=289&&g.vx<0);
        g.ballX=200;g.ballY=272;g.vx=0;g.vy=140;g.advance(.03f);
        assert(g.vy<0&&g.vx>0&&g.ballY<275);
        g.reset();g.mode=GameBricks::PLAYING;g.ballX=92;g.ballY=180;g.vx=0;g.vy=-150;g.advance(.05f);
        assert(g.remaining==19&&g.score==10&&g.best==10&&g.vy>0&&!g.bricks[15]);
        g.advance(.005f);assert(g.score==10);
        g.redraw=false;g.ballX=76;g.ballY=303.8f;g.vy=160;g.vx=0;g.advance(.01f);
        assert(g.lives==2&&g.mode==GameBricks::SERVING);
        g.render();assert(g.dirtyY0==0&&g.dirtyY1==359); // A stopped ball still needs its new screen.
        g.mode=GameBricks::PLAYING;g.lives=1;g.ballX=76;g.ballY=303.8f;g.vy=160;g.advance(.01f);
        assert(!g.lives&&g.mode==GameBricks::LOST);
        g.reset();g.mode=GameBricks::PLAYING;for(auto &b:g.bricks)b=false;
        g.redraw=false;g.bricks[0]=true;g.remaining=1;g.ballX=92;g.ballY=120;g.vx=0;g.vy=-150;g.advance(.05f);
        assert(g.mode==GameBricks::WON&&g.remaining==0);
        g.render();assert(g.dirtyY0==0&&g.dirtyY1==359);
        g.reset();g.mode=GameBricks::PLAYING;g.ballX=92;g.ballY=180;g.vx=0;g.vy=-240;
        g.update(.08f);assert(g.score==10&&g.vy>0); // Substeps prevent tunnelling.
        // Complete a whole round with an ideal paddle, exercising repeated
        // wall/brick/paddle contacts rather than only isolated collisions.
        g.reset();g.serve();int frames=0;
        for(;frames<120*240&&g.mode!=GameBricks::WON&&g.mode!=GameBricks::LOST;++frames){
            g.paddle=fmaxf(96,fminf(264,g.ballX-sinf(frames*.006f)*19));
            if(g.mode==GameBricks::SERVING)g.serve();
            g.update(1.f/120);
            assert(std::isfinite(g.ballX)&&std::isfinite(g.ballY)&&g.remaining>=0&&g.lives>0);
        }
        assert(g.mode==GameBricks::WON&&g.score==200&&g.remaining==0);
    }
    static int mode(const GameSnake &g){return g.mode;}
    static int mode(const GameBricks &g){return g.mode;}
    static int head(const GameSnake &g){return g.body[0];}
    static int turns(const GameSnake &g){return g.turnCount;}
    static int turn(const GameSnake &g){return g.turns[0];}
    static void snakeHeading(GameSnake &g,int heading,int cell=138){
        g.reset();g.mode=GameSnake::PLAYING;g.direction=heading;
        for(int i=0;i<g.length;++i)g.body[i]=cell-i;
    }
    static void serving(GameBricks &g){g.mode=GameBricks::SERVING;g.ballX=g.paddle;g.ballY=274;g.redraw=true;}
    static float paddle(const GameBricks &g){return g.paddle;}
    static float x(const GameBricks &g){return g.ballX;}
    static float y(const GameBricks &g){return g.ballY;}
    static void snakeFixture(GameSnake &g){
        g.reset();g.mode=GameSnake::PLAYING;g.length=15;g.direction=0;g.score=11;g.best=18;g.food=4*15+4;
        const int cells[]={130,145,144,143,142,141,140,125,110,95,96,97,98,83,68};
        for(int i=0;i<15;++i)g.body[i]=cells[i];g.redraw=true;
    }
    static void bricksFixture(GameBricks &g){
        g.reset();g.mode=GameBricks::PLAYING;g.score=60;g.best=120;g.lives=2;
        for(int i:{9,11,14,16,18,19})g.bricks[i]=false;
        g.remaining=14;g.ballX=215;g.ballY=210;g.paddle=198;g.redraw=true;
    }
};
static void frame(Game &g,float dt=.001f){testMillis+=unsigned(dt*1000);g.update(dt);g.render();}
static void tap(Game &g,int x,int y){
    px=x;py=y;finger=true;released=false;frame(g);finger=false;released=true;frame(g);released=false;
}
static void ppm(const char *path){
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n360 360\n255\n");
    for(int y=0;y<360;++y)for(int x=0;x<360;++x){
        uint16_t c=PX(fb[y*360+x]);unsigned char rgb[]={uint8_t(((c>>11)&31)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};
        if((x-180)*(x-180)+(y-180)*(y-180)>180*180)rgb[0]=rgb[1]=rgb[2]=0;
        fwrite(rgb,1,3,f);
    }fclose(f);
}
int main(){
    // Aligned sentinels catch any drawing outside the actual framebuffer.
    std::vector<uint16_t> pixels(360*360+16,0x53a7);fb=pixels.data()+8;
    GameSnake snake;snake.enter();snake.render();ppm("build/snake-ready.ppm");
    assert(snake.dirtyY0==0&&snake.dirtyY1==359);snake.render();assert(snake.dirtyY1<snake.dirtyY0);
    MiniGamesTestAccess::snakeChecks(snake);
    tap(snake,180,240);assert(MiniGamesTestAccess::mode(snake)==1);
    snake.enter();tap(snake,180,240);assert(MiniGamesTestAccess::mode(snake)==1);
    // Taps are relative to the moving head, not the centre of the display.
    // Here the head is at (116,212): (260,190) is above it despite a large X gap.
    MiniGamesTestAccess::snakeHeading(snake,1);tap(snake,260,190);
    assert(MiniGamesTestAccess::turns(snake)==1&&MiniGamesTestAccess::turn(snake)==0);
    MiniGamesTestAccess::snakeHeading(snake,3);tap(snake,140,235);
    assert(MiniGamesTestAccess::turn(snake)==2);
    MiniGamesTestAccess::snakeHeading(snake,0);tap(snake,140,190);
    assert(MiniGamesTestAccess::turn(snake)==1);
    MiniGamesTestAccess::snakeHeading(snake,2);tap(snake,90,235);
    assert(MiniGamesTestAccess::turn(snake)==3);
    MiniGamesTestAccess::snakeHeading(snake,1);tap(snake,116,212);
    assert(MiniGamesTestAccess::turns(snake)==0);
    MiniGamesTestAccess::snakeHeading(snake,1);tap(snake,116,170);tap(snake,70,212);
    assert(MiniGamesTestAccess::turns(snake)==2&&MiniGamesTestAccess::turn(snake)==0);
    MiniGamesTestAccess::snakeHeading(snake,1);
    // A drag is not also interpreted as a tap/turn at release.
    finger=true;px=180;py=190;frame(snake);py=140;frame(snake);
    finger=false;released=true;frame(snake);released=false;assert(MiniGamesTestAccess::turns(snake)==0);
    tap(snake,180,326);assert(MiniGamesTestAccess::mode(snake)==2);
    const int head=MiniGamesTestAccess::head(snake);for(int i=0;i<100;++i)frame(snake,.02f);
    assert(MiniGamesTestAccess::head(snake)==head&&snake.dirtyY1<snake.dirtyY0);
    tap(snake,180,326);assert(MiniGamesTestAccess::mode(snake)==1);
    snake.update(NAN);snake.update(-1);snake.update(INFINITY);assert(MiniGamesTestAccess::head(snake)==head);
    MiniGamesTestAccess::snakeFixture(snake);snake.render();ppm("build/snake-playing.ppm");
    GameBricks bricks;bricks.enter();bricks.render();ppm("build/bricks-ready.ppm");
    MiniGamesTestAccess::bricksChecks(bricks);
    tap(bricks,180,240);assert(MiniGamesTestAccess::mode(bricks)==2);
    finger=released=false;bricks.enter();tap(bricks,180,240);assert(MiniGamesTestAccess::mode(bricks)==2);
    tap(bricks,110,316);assert(MiniGamesTestAccess::paddle(bricks)==162);
    finger=true;px=110;py=316;frame(bricks);assert(MiniGamesTestAccess::paddle(bricks)==144);
    for(int i=0;i<5;++i)frame(bricks,.02f);assert(MiniGamesTestAccess::paddle(bricks)==144);
    for(int i=0;i<50;++i)frame(bricks,.02f);assert(MiniGamesTestAccess::paddle(bricks)==96);
    finger=false;released=true;frame(bricks);released=false;
    tap(bricks,250,316);assert(MiniGamesTestAccess::paddle(bricks)==114);
    finger=true;px=250;py=316;frame(bricks);for(int i=0;i<50;++i)frame(bricks,.02f);
    assert(MiniGamesTestAccess::paddle(bricks)==264);finger=false;released=true;frame(bricks);released=false;
    bricks.enter();tap(bricks,180,240);
    tap(bricks,180,316);assert(MiniGamesTestAccess::mode(bricks)==3);
    const float x=MiniGamesTestAccess::x(bricks),y=MiniGamesTestAccess::y(bricks);
    for(int i=0;i<100;++i)frame(bricks,.02f);
    assert(MiniGamesTestAccess::x(bricks)==x&&MiniGamesTestAccess::y(bricks)==y&&bricks.dirtyY1<bricks.dirtyY0);
    const float parked=MiniGamesTestAccess::paddle(bricks);tap(bricks,110,316);assert(MiniGamesTestAccess::paddle(bricks)==parked);
    tap(bricks,180,316);assert(MiniGamesTestAccess::mode(bricks)==2);
    MiniGamesTestAccess::serving(bricks);tap(bricks,110,316);
    assert(MiniGamesTestAccess::mode(bricks)==1&&MiniGamesTestAccess::x(bricks)==MiniGamesTestAccess::paddle(bricks));
    tap(bricks,180,316);assert(MiniGamesTestAccess::mode(bricks)==2);
    bricks.update(NAN);bricks.update(-1);bricks.update(INFINITY);
    assert(std::isfinite(MiniGamesTestAccess::x(bricks))&&std::isfinite(MiniGamesTestAccess::y(bricks)));
    MiniGamesTestAccess::bricksFixture(bricks);bricks.render();ppm("build/bricks-playing.ppm");
    bricks.render();assert(bricks.dirtyY1<bricks.dirtyY0);
    testMillis+=33;bricks.render();assert(bricks.dirtyY0==0&&bricks.dirtyY1==359);
    testMillis=0xfffffff0;bricks.render();testMillis=24;bricks.render();
    assert(bricks.dirtyY0==0&&bricks.dirtyY1==359); // Render pacing survives millis wrap.
    for(int i=0;i<8;++i)assert(pixels[i]==0x53a7&&pixels[pixels.size()-1-i]==0x53a7);
    puts("PASS: head-relative snake taps/turns/growth/collisions/win; bricks button taps/hold/clamps/serve/pause, rebounds/lives/full-round win/substeps; guarded framebuffer and timing");
}
