#include "game_snake.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "input.h"

static const uint16_t COL_BG=PX(RGB565(0x0D,0x13,0x1C));
static const uint16_t COL_GRID=PX(RGB565(0x19,0x26,0x30));
static const uint16_t COL_TEXT=PX(RGB565(0xED,0xE5,0xD8));
static const uint16_t COL_DIM=PX(RGB565(0x91,0xA7,0xA8));
static const uint16_t COL_FOOD=PX(RGB565(0xF4,0xAD,0x68));
// The whole bottom strip is a pause target, including the blank margin around
// its label. Activate on contact, so finger drift cannot cancel the action.
static bool snakePause(int x,int y){return x>=0&&x<SCR_W&&y>=298&&y<SCR_H;}
static bool snakeStart(int x,int y){return x>=128&&x<=232&&y>=224&&y<=267;}

void GameSnake::reset(){
    length=4;score=0;direction=1;turnCount=0;elapsed=0;
    for(int i=0;i<length;++i)body[i]=7*SIDE+7-i;
    placeFood();redraw=true;
}
void GameSnake::enter(){
    reset();mode=READY;tracking=moved=pauseTouch=pressed=pauseSeen=false;pauseChanges=0;lastPauseMs=0;
}
void GameSnake::placeFood(){
    if(length==CAPACITY){food=-1;mode=WON;return;}
    int freeIndex=int(random(0,CAPACITY-length));
    for(int cell=0;cell<CAPACITY;++cell){
        bool occupied=false;for(int i=0;i<length;++i)if(body[i]==cell){occupied=true;break;}
        if(!occupied && freeIndex--==0){food=cell;return;}
    }
}
void GameSnake::queueTurn(int next){
    const int previous=turnCount?turns[turnCount-1]:direction;
    if(turnCount==2||next==previous||(next+2)%4==previous)return;
    turns[turnCount++]=next;redraw=true;
}
void GameSnake::step(){
    if(turnCount){direction=turns[0];turns[0]=turns[1];--turnCount;}
    const int dx[]={0,1,0,-1},dy[]={-1,0,1,0};
    const int x=body[0]%SIDE+dx[direction],y=body[0]/SIDE+dy[direction];
    redraw=true;
    if(x<0||x>=SIDE||y<0||y>=SIDE){mode=LOST;return;}
    const int next=y*SIDE+x;const bool grow=next==food;
    // The departing tail is free unless eating on this move keeps it in place.
    for(int i=0;i<length-(grow?0:1);++i)if(body[i]==next){mode=LOST;return;}
    memmove(body+1,body,(length-(grow?0:1))*sizeof body[0]);body[0]=next;
    if(grow){++length;++score;if(score>best)best=score;placeFood();}
}
void GameSnake::update(float dt){
    if(!isfinite(dt)||dt<0)dt=0;if(dt>.08f)dt=.08f;
    const bool oldPressed=pressed;
    if(input::touchDown()){
        const int x=input::tapX(),y=input::tapY();
        if(!tracking){
            tracking=true;startX=x;startY=y;moved=false;pauseTouch=snakePause(x,y);
            if(pauseTouch&&(mode==PLAYING||mode==PAUSED)&&(!pauseSeen||uint32_t(millis()-lastPauseMs)>=220)){
                mode=mode==PAUSED?PLAYING:PAUSED;elapsed=0;redraw=true;
                pauseSeen=true;lastPauseMs=millis();++pauseChanges;
                Serial.printf("[snake] pause-touch x=%d y=%d mode=%d\n",x,y,mode);
            }
        }
        const int dx=x-startX,dy=y-startY;
        if(dx*dx+dy*dy>24*24)moved=true;
        pressed=pauseTouch&&(mode==PLAYING||mode==PAUSED);
        if(!pauseTouch)pressed=!moved&&mode!=PLAYING&&mode!=PAUSED&&snakeStart(startX,startY)&&snakeStart(x,y);
    }else{
        if(tracking&&!moved&&input::tapped()){
            const int x=input::tapX(),y=input::tapY();
            if(mode==PLAYING&&!pauseTouch){
                const int heading=turnCount?turns[turnCount-1]:direction;
                const int dx=x-(68+(body[0]%SIDE)*16),dy=y-(68+(body[0]/SIDE)*16);
                // A horizontal snake accepts taps above/below its head; a
                // vertical snake accepts taps left/right. No swipe required.
                if((heading==1||heading==3)&&abs(dy)>=6)queueTurn(dy<0?0:2);
                else if((heading==0||heading==2)&&abs(dx)>=6)queueTurn(dx<0?3:1);
            }else if((mode==READY||mode==LOST||mode==WON)&&snakeStart(startX,startY)&&snakeStart(x,y)){
                reset();mode=PLAYING;redraw=true;
            }
        }
        tracking=pressed=false;
    }
    if(oldPressed!=pressed)redraw=true;
    if(mode==PLAYING){
        elapsed+=dt;
        const float interval=fmaxf(.11f,.24f-score*.006f);
        if(elapsed>=interval){elapsed-=interval;step();}
    }
}
void GameSnake::render(){
    if(!redraw){setDirty(1,0);return;}redraw=false;setDirty(0,359);
    gfx::clear(COL_BG);gfx::textCentered("SNAKE",24,accent(),2);
    char text[32];snprintf(text,sizeof text,"SCORE %02d    BEST %02d",score,best);gfx::textCentered(text,48,COL_DIM);
    for(int i=0;i<=SIDE;++i){gfx::line(60+i*16,60,60+i*16,300,COL_GRID);gfx::line(60,60+i*16,300,60+i*16,COL_GRID);}
    if(food>=0){const int x=68+(food%SIDE)*16,y=68+(food/SIDE)*16;
        gfx::fillCircle(x,y+1,5,COL_FOOD);gfx::fillRect(x,y-6,2,4,accent());}
    for(int i=length-1;i>=0;--i){
        const int x=62+(body[i]%SIDE)*16,y=62+(body[i]/SIDE)*16;
        gfx::fillRect(x,y,13,13,i?gfx::dim(accent(),.18f):accent());
        if(!i){
            const int heading=turnCount?turns[0]:direction;
            if(heading==0||heading==2){const int ey=y+(heading==0?3:8);gfx::fillRect(x+3,ey,2,2,COL_BG);gfx::fillRect(x+8,ey,2,2,COL_BG);}
            else{const int ex=x+(heading==1?8:3);gfx::fillRect(ex,y+3,2,2,COL_BG);gfx::fillRect(ex,y+8,2,2,COL_BG);}
        }
    }
    if(mode==READY||mode==LOST||mode==WON){
        gfx::fillRect(85,99,190,174,COL_BG);
        gfx::textCentered(mode==READY?"SNAKE":mode==WON?"YOU WIN":"GAME OVER",116,COL_TEXT,2);
        gfx::textCentered("TAP AROUND THE HEAD",156,COL_DIM);
        gfx::textCentered(mode==READY?"EAT THE ORANGE DOTS":"AVOID WALLS AND TAIL",177,COL_DIM);
        gfx::textCentered("NO REVERSE TURNS",198,COL_DIM);
        gfx::fillRect(128,224,104,40,pressed?accent():COL_GRID);
        gfx::textCentered(mode==READY?"PLAY":"RETRY",237,pressed?COL_BG:COL_TEXT,2);
        gfx::textCentered("HOLD BOOT TO EXIT",324,COL_DIM);
    }else{
        gfx::fillRect(110,308,140,36,pressed&&pauseTouch?accent():COL_GRID);
        gfx::textCentered(mode==PAUSED?"RESUME":"PAUSE",324,pressed&&pauseTouch?COL_BG:COL_TEXT);
        if(mode==PAUSED){gfx::fillRect(100,152,160,51,COL_BG);gfx::textCentered("PAUSED",170,COL_TEXT,2);}
    }
}
void GameSnake::drawIcon(float cx,float cy,float size) const{
    gfx::fillCircle(cx,cy,size*.49f,gfx::dim(accent(),.75f));
    const float u=size*.12f;
    gfx::fillRect(cx-2*u,cy-u,4*u,u,accent());gfx::fillRect(cx-2*u,cy-u,u,3*u,accent());
    gfx::fillRect(cx-2*u,cy+u,3*u,u,accent());gfx::fillRect(cx+u,cy-2*u,1.5f*u,1.5f*u,accent());
    gfx::fillCircle(cx+2*u,cy-1.5f*u,size*.025f,PX(RGB565(0x10,0x18,0x20)));
}
void GameSnake::printDiagnostics() const{
    Serial.printf("[snake] mode=%d score=%d best=%d length=%d head=%u food=%d turns=%d pauses=%u input=%d,%d footer=%d\n",mode,score,best,length,body[0],food,turnCount,pauseChanges,startX,startY,pauseTouch);
}
