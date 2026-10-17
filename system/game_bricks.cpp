#include "game_bricks.h"
#include <Arduino.h>
#include <math.h>
#include "input.h"

static const uint16_t COL_BG=PX(RGB565(0x12,0x13,0x21));
static const uint16_t COL_TRACK=PX(RGB565(0x2C,0x2B,0x3D));
static const uint16_t COL_TEXT=PX(RGB565(0xEE,0xE5,0xD9));
static const uint16_t COL_DIM=PX(RGB565(0xA7,0xA2,0xB0));
static const uint16_t COL_BALL=PX(RGB565(0xA4,0xDD,0xD0));
static const float BALL_R=5,PADDLE_Y=280,PADDLE_HALF=30;
static int bricksControl(int x,int y){
    if(y>=298&&x>=104&&x<=256)return 1;
    return x<180?0:2;
}
static bool bricksStart(int x,int y){return x>=128&&x<=232&&y>=224&&y<=267;}
static bool circleBox(float x,float y,float left,float top,float width,float height){
    const float dx=x-fmaxf(left,fminf(left+width,x)),dy=y-fmaxf(top,fminf(top+height,y));
    return dx*dx+dy*dy<=BALL_R*BALL_R;
}
void GameBricks::reset(){
    for(auto &brick:bricks)brick=true;
    score=0;lives=3;remaining=20;paddle=180;ballX=paddle;ballY=PADDLE_Y-BALL_R-1;
    vx=78;vy=-140;redraw=true;
}
void GameBricks::enter(){
    reset();mode=READY;resumeMode=PLAYING;tracking=moved=painted=controlSeen=false;lastFrame=lastControlMs=0;pressed=startTarget=-1;held=0;
}
void GameBricks::serve(){
    ballX=paddle;ballY=PADDLE_Y-BALL_R-1;
    vx=(score%2?-1:1)*78;vy=-140;mode=PLAYING;redraw=true;
}
void GameBricks::movePaddle(float delta){
    const float next=fmaxf(96,fminf(264,paddle+delta));
    if(next!=paddle){paddle=next;redraw=true;}
    if(mode==SERVING)ballX=paddle;
}
void GameBricks::advance(float dt){
    const float oldX=ballX,oldY=ballY;
    ballX+=vx*dt;ballY+=vy*dt;
    if(ballX<66+BALL_R){ballX=66+BALL_R;vx=fabsf(vx);}
    if(ballX>294-BALL_R){ballX=294-BALL_R;vx=-fabsf(vx);}
    if(ballY<80+BALL_R){ballY=80+BALL_R;vy=fabsf(vy);}
    if(vy>0&&oldY<=PADDLE_Y&&circleBox(ballX,ballY,paddle-PADDLE_HALF,PADDLE_Y,2*PADDLE_HALF,8)){
        ballY=PADDLE_Y-BALL_R-.1f;
        const float speed=fminf(240,sqrtf(vx*vx+vy*vy)+3);
        float offset=fmaxf(-1,fminf(1,(ballX-paddle)/PADDLE_HALF));
        if(fabsf(offset)<.12f)offset=offset<0?-.12f:.12f;
        vx=offset*speed*.85f;vy=-sqrtf(speed*speed-vx*vx);
    }
    for(int i=0;i<20;++i)if(bricks[i]){
        const float x=72+(i%5)*44,y=100+(i/5)*19;
        if(!circleBox(ballX,ballY,x,y,40,14))continue;
        bricks[i]=false;--remaining;score+=10;if(score>best)best=score;
        if(oldY<=y-BALL_R){ballY=y-BALL_R-.1f;vy=-fabsf(vy);}
        else if(oldY>=y+14+BALL_R){ballY=y+14+BALL_R+.1f;vy=fabsf(vy);}
        else if(oldX<x){ballX=x-BALL_R-.1f;vx=-fabsf(vx);}
        else{ballX=x+40+BALL_R+.1f;vx=fabsf(vx);}
        const float speed=sqrtf(vx*vx+vy*vy),gain=fminf(1.015f,240/speed);
        vx*=gain;vy*=gain;
        if(!remaining){mode=WON;redraw=true;}
        break;
    }
    if(ballY>304){
        --lives;mode=lives?SERVING:LOST;ballX=paddle;ballY=PADDLE_Y-BALL_R-1;redraw=true;
    }
}
void GameBricks::update(float dt){
    if(!isfinite(dt)||dt<0)dt=0;if(dt>.08f)dt=.08f;
    const int oldPressed=pressed;
    if(input::touchDown()){
        const int x=input::tapX(),y=input::tapY();
        if(!tracking){
            tracking=true;startX=x;startY=y;moved=false;held=0;startTarget=bricksControl(x,y);
            if((mode==PLAYING||mode==SERVING)&&(startTarget==0||startTarget==2))movePaddle(startTarget==0?-18:18);
            if(startTarget==1&&(mode==PLAYING||mode==SERVING||mode==PAUSED)&&(!controlSeen||uint32_t(millis()-lastControlMs)>=220)){
                if(mode==SERVING)serve();
                else if(mode==PAUSED)mode=resumeMode;
                else{resumeMode=mode;mode=PAUSED;}
                controlSeen=true;lastControlMs=millis();redraw=true;
            }
        }
        const int dx=x-startX,dy=y-startY;if(dx*dx+dy*dy>24*24)moved=true;
        pressed=-1;
        if(mode==PLAYING||mode==SERVING||mode==PAUSED){
            const int current=bricksControl(x,y);
            if(startTarget==1)pressed=1;
            else if(mode!=PAUSED&&current!=1){
                if(current!=startTarget){startTarget=current;held=0;movePaddle(current==0?-18:18);}
                pressed=current;
            }
        }else if(!moved&&bricksStart(startX,startY)&&bricksStart(x,y)){
            pressed=3;
        }
        if((mode==PLAYING||mode==SERVING)&&(pressed==0||pressed==2)){
            const float repeat=fmaxf(0,held+dt-.18f)-fmaxf(0,held-.18f);
            held+=dt;movePaddle((pressed==0?-1:1)*repeat*240);
        }
    }else{
        if(tracking&&input::tapped()){
            const int x=input::tapX(),y=input::tapY();
            if(!moved&&(mode==READY||mode==LOST||mode==WON)&&bricksStart(startX,startY)&&bricksStart(x,y)){
                reset();serve();
            }
        }
        tracking=false;pressed=-1;
    }
    if(oldPressed!=pressed)redraw=true;
    if(mode==SERVING)ballX=paddle;
    if(mode==PLAYING&&dt>0){
        const int steps=int(ceilf(dt/.008f));
        for(int i=0;i<steps&&mode==PLAYING;++i)advance(dt/steps);
    }
}
void GameBricks::render(){
    const uint32_t now=millis();
    if(painted&&!redraw&&(mode!=PLAYING||uint32_t(now-lastFrame)<33)){setDirty(1,0);return;}
    painted=true;redraw=false;lastFrame=now;setDirty(0,359);
    gfx::clear(COL_BG);gfx::textCentered("BRICKS",24,accent(),2);
    char text[32];snprintf(text,sizeof text,"SCORE %03d  BEST %03d",score,best);gfx::textCentered(text,48,COL_DIM);
    for(int i=0;i<3;++i)gfx::fillCircle(170+i*10,68,3,i<lives?COL_BALL:COL_TRACK);
    gfx::line(66,80,294,80,COL_TRACK);gfx::line(66,80,66,300,pressed==0?accent():COL_TRACK);gfx::line(294,80,294,300,pressed==2?accent():COL_TRACK);
    for(int i=0;i<20;++i)if(bricks[i]){
        const uint16_t colors[]={PX(RGB565(0xED,0xA5,0x85)),PX(RGB565(0xE3,0xC6,0x8B)),PX(RGB565(0xA1,0xC6,0xB2)),PX(RGB565(0xA5,0xB1,0xD5))};
        const int x=72+(i%5)*44,y=100+(i/5)*19;
        gfx::fillRect(x,y,40,14,colors[i/5]);gfx::fillRect(x+2,y+2,36,2,gfx::mix(colors[i/5],COL_TEXT,.3f));
    }
    gfx::fillRect(int(paddle-PADDLE_HALF),PADDLE_Y,2*PADDLE_HALF,8,COL_BALL);
    gfx::fillCircle(ballX,ballY,BALL_R,COL_TEXT);
    if(mode==READY||mode==LOST||mode==WON){
        gfx::fillRect(85,99,190,174,COL_BG);
        gfx::textCentered(mode==READY?"BRICKS":mode==WON?"YOU WIN":"GAME OVER",116,COL_TEXT,2);
        gfx::textCentered("TAP LEFT / RIGHT HALF",158,COL_DIM);
        gfx::textCentered("BREAK ALL 20 BRICKS",180,COL_DIM);
        gfx::textCentered("THREE LIVES",201,COL_DIM);
        gfx::fillRect(128,224,104,40,pressed==3?accent():COL_TRACK);
        gfx::textCentered(mode==READY?"PLAY":"RETRY",237,pressed==3?COL_BG:COL_TEXT,2);
        gfx::textCentered("HOLD BOOT TO EXIT",324,COL_DIM);
    }else{
        gfx::fillRect(110,308,140,36,pressed==1?accent():COL_TRACK);
        gfx::textCentered(mode==PLAYING?"PAUSE":mode==PAUSED?"RESUME":"PLAY",324,pressed==1?COL_BG:COL_TEXT);
        if(mode==PAUSED){gfx::fillRect(100,194,160,36,COL_BG);gfx::textCentered("PAUSED",204,COL_TEXT,2);}
        if(mode==SERVING)gfx::textCentered("TAP PLAY TO SERVE",222,COL_DIM);
    }
}
void GameBricks::drawIcon(float cx,float cy,float size) const{
    gfx::fillCircle(cx,cy,size*.49f,gfx::dim(accent(),.75f));
    for(int row=0;row<2;++row)for(int col=0;col<3;++col)
        gfx::fillRect(cx-size*.30f+col*size*.21f,cy-size*.25f+row*size*.15f,size*.18f,size*.11f,accent());
    gfx::fillCircle(cx+size*.10f,cy+size*.13f,size*.05f,PX(RGB565(0xA4,0xDD,0xD0)));
    gfx::fillRect(cx-size*.24f,cy+size*.28f,size*.48f,size*.07f,PX(RGB565(0xA4,0xDD,0xD0)));
}
void GameBricks::printDiagnostics() const{
    Serial.printf("[bricks] mode=%d score=%d best=%d lives=%d remaining=%d ball=%.1f,%.1f velocity=%.1f,%.1f paddle=%.1f\n",mode,score,best,lives,remaining,ballX,ballY,vx,vy,paddle);
}
