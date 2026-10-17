#include "game_hypno.h"
#include <Arduino.h>
#include <math.h>
#include "input.h"

static const uint16_t COL_BG=PX(RGB565(0x0B,0x0B,0x16));
static const uint16_t COL_RING=PX(RGB565(0x29,0x22,0x42));
static const uint16_t COL_GOLD=PX(RGB565(0xE8,0xB7,0x68));
static const uint16_t COL_LIGHT=PX(RGB565(0xFF,0xDF,0x9A));
static const uint16_t COL_BRASS=PX(RGB565(0x83,0x54,0x3E));
static const uint16_t COL_FACE=PX(RGB565(0xED,0xE3,0xC8));
static const uint16_t COL_INK=PX(RGB565(0x40,0x32,0x49));
static const uint16_t COL_TEXT=PX(RGB565(0xD4,0xC4,0xE8));
static const uint16_t COL_DIM=PX(RGB565(0x75,0x67,0x8E));
static const float PERIODS[]={3.8f,2.8f,1.9f};
static const char *const SPEEDS[]={"SLOW","NORMAL","FAST"};

// Small line segments avoid scanning a whole disc for every faint background ring.
static void haloRing(float radius,uint16_t color) {
    static float ux[65],uy[65];
    static bool ready=false;
    if (!ready) {
        for(int i=0;i<=64;++i) {
            const float a=i*(2*PI_F/64);
            ux[i]=cosf(a);uy[i]=sinf(a);
        }
        ready=true;
    }
    for(int i=0;i<64;++i)
        gfx::line(180+ux[i]*radius,184+uy[i]*radius,
                  180+ux[i+1]*radius,184+uy[i+1]*radius,color);
}

static int targetAt(int x,int y) {
    if(y>=276 && y<=334) {
        for(int i=0;i<3;++i) {
            const int dx=x-(112+i*68),dy=y-306;
            if(dx*dx+dy*dy<=27*27)return i;
        }
    }
    return 3; // tapping the scene also pauses; dragging does not.
}

void GameHypno::drawIcon(float cx,float cy,float size) const {
    const float r=size*.5f;
    gfx::fillCircle(cx,cy,r,accent());
    gfx::strokeCircle(cx,cy,r-1.5f,1.5f,PX(RGB565(0xE3,0xC8,0xFF)));
    gfx::line(cx-r*.32f,cy-r*.70f,cx+r*.15f,cy-r*.05f,COL_LIGHT);
    gfx::strokeCircle(cx+r*.18f,cy-r*.10f,r*.12f,1.5f,COL_GOLD);
    gfx::fillCircle(cx+r*.27f,cy+r*.29f,r*.43f,COL_BRASS);
    gfx::fillCircle(cx+r*.27f,cy+r*.26f,r*.40f,COL_GOLD);
    gfx::fillCircle(cx+r*.27f,cy+r*.26f,r*.31f,COL_FACE);
    gfx::line(cx+r*.27f,cy+r*.26f,cx+r*.08f,cy+r*.12f,COL_INK);
    gfx::line(cx+r*.27f,cy+r*.26f,cx+r*.40f,cy+r*.02f,COL_INK);
    gfx::fillCircle(cx+r*.27f,cy+r*.26f,1.5f,COL_INK);
}

void GameHypno::enter() {
    phase=halo=0;speed=1;paused=tracking=moved=painted=false;
    pressed=startTarget=-1;redraw=true;lastFrame=0;
    setDirty(0,SCR_H-1);
}
void GameHypno::leave() {tracking=false;pressed=-1;}

void GameHypno::update(float dt) {
    const int oldPressed=pressed;
    if(input::touchDown()) {
        const int x=input::tapX(),y=input::tapY();
        if(!tracking) {
            tracking=true;moved=false;startX=x;startY=y;startTarget=targetAt(x,y);
        }
        const int dx=x-startX,dy=y-startY;
        if(dx*dx+dy*dy>14*14)moved=true;
        pressed=(!moved && targetAt(x,y)==startTarget)?startTarget:-1;
    } else {
        if(tracking && !moved && input::tapped() &&
           targetAt(input::tapX(),input::tapY())==startTarget) {
            if(startTarget==0) {if(speed>0)--speed;}
            else if(startTarget==2) {if(speed<2)++speed;}
            else paused=!paused;
            redraw=true;
        }
        tracking=false;pressed=-1;
    }
    if(oldPressed!=pressed)redraw=true;
    if(!paused && dt>0 && isfinite(dt)) {
        dt=fminf(dt,.1f);
        phase=fmodf(phase+dt*(2*PI_F/PERIODS[speed]),2*PI_F);
        halo=fmodf(halo+dt*.20f,1.0f);
    }
}

void GameHypno::render() {
    const uint32_t now=millis();
    if(painted && !redraw && (paused || uint32_t(now-lastFrame)<33u)) {
        setDirty(1,0);return;
    }
    lastFrame=now;painted=true;redraw=false;setDirty(0,SCR_H-1);
    gfx::clear(COL_BG);
    for(int i=0;i<5;++i) {
        const float r=20+((i+1)-halo)*30;
        const float fade=fminf((r-20)/25,(180-r)/35);
        haloRing(r,gfx::mix(COL_BG,COL_RING,fmaxf(0,fminf(1,fade))));
    }
    // Sparse, fixed pixels keep the launcher palette without a second animation.
    for(int i=0;i<24;++i) {
        const int x=26+(i*83)%308,y=67+(i*47)%206;
        gfx::pixel(x,y,COL_RING);
    }
    gfx::fillRect(120,26,120,28,COL_BG);
    gfx::textCentered("HYPNO",32,COL_TEXT,2);

    const float angle=.64f*sinf(phase),sx=sinf(angle),sy=cosf(angle);
    const float cx=180+sx*130,cy=75+sy*130;
    // Linked chain with a stationary top anchor.
    for(int i=0;i<13;++i) {
        const float d=6+i*5.5f;
        const float x=180+sx*d,y=75+sy*d;
        gfx::strokeCircle(x,y,3.6f,1.5f,(i&1)?COL_BRASS:COL_GOLD);
    }
    gfx::fillCircle(180,75,4,COL_GOLD);
    gfx::fillCircle(179,74,1.4f,COL_LIGHT);
    const float crownX=cx-sx*53,crownY=cy-sy*53;
    gfx::strokeCircle(crownX,crownY,8,3,COL_GOLD);
    gfx::line(crownX-sy*6+sx*7,crownY+sx*6+sy*7,
              crownX+sy*6+sx*7,crownY-sx*6+sy*7,COL_LIGHT);

    gfx::fillCircle(cx+3,cy+6,46,PX(RGB565(0x07,0x07,0x10)));
    gfx::fillCircle(cx,cy,46,COL_BRASS);
    gfx::fillCircle(cx-1,cy-1,43.5f,COL_GOLD);
    gfx::strokeCircle(cx-1,cy-1,41.5f,1.5f,COL_LIGHT);
    gfx::fillCircle(cx,cy,38,COL_INK);
    gfx::fillCircle(cx,cy,36,COL_FACE);
    for(int i=0;i<60;++i) {
        const float a=i*(2*PI_F/60)+angle;
        const float dx=sinf(a),dy=-cosf(a);
        const float inner=(i%5==0)?29:32.5f;
        gfx::line(cx+dx*inner,cy+dy*inner,cx+dx*34,cy+dy*34,
                  (i%5==0)?COL_INK:COL_BRASS);
    }
    // A classic 10:10 dial rotates with the hanging watch.
    for(int i=0;i<2;++i) {
        const float a=angle+(i==0?-.98f:1.04f);
        const float ex=cx+sinf(a)*(i==0?20:26),ey=cy-cosf(a)*(i==0?20:26);
        gfx::line(cx,cy,ex,ey,COL_INK);
        gfx::line(cx+1,cy,ex+1,ey,COL_INK);
    }
    gfx::fillCircle(cx,cy,3,COL_BRASS);
    gfx::fillCircle(cx,cy,1.2f,COL_LIGHT);

    gfx::fillRect(61,271,238,65,COL_BG);
    gfx::textCentered(SPEEDS[speed],270,COL_DIM,1);
    for(int i=0;i<3;++i) {
        const int x=112+i*68;
        const bool disabled=(i==0 && speed==0)||(i==2 && speed==2);
        const bool down=pressed==i;
        const uint16_t ink=disabled?COL_RING:(down?COL_BG:COL_TEXT);
        gfx::fillCircle(x,306,21,down?COL_TEXT:PX(RGB565(0x17,0x13,0x26)));
        gfx::strokeCircle(x,306,21,1,disabled?COL_RING:COL_DIM);
        if(i!=1) {
            gfx::fillRect(x-5,305,11,2,ink);
            if(i==2)gfx::fillRect(x-1,301,2,10,ink);
        } else if(paused) {
            for(int row=-6;row<=6;++row)
                gfx::fillRect(x-3,306+row,9-abs(row),1,ink);
        } else {
            gfx::fillRect(x-5,301,3,11,ink);gfx::fillRect(x+2,301,3,11,ink);
        }
    }
    gfx::textCentered(paused?"TAP TO CONTINUE":"FOLLOW THE SWING",340,COL_DIM,1);
}
