#include "outfits.h"
#include "gfx.h"

namespace outfits {
static const char* const NAMES[] = {
    "FLOWER", "HARD HAT", "HEADPHONES", "THINKER", "BUBBLES",
    "SLEEPY", "CODER", "READER", "DELIVERY", "BOW",
    "CROWN", "BERET", "HALO", "STAR", "WIZARD",
    "HEART", "CAP", "EARMUFFS", "RAINBOW", "ANTENNA"
};
uint8_t count() { return sizeof(NAMES)/sizeof(NAMES[0]); }
const char* name(uint8_t index) { return index < count() ? NAMES[index] : ""; }
uint32_t duration(uint8_t index) { return index < count() ? 2000 : 0; }
bool validate(uint8_t index) { return index < count(); }
uint32_t resourceBytes() { return 0; }

static const uint16_t GREEN = PX(RGB565(115,203,103));
static const uint16_t STEM = PX(RGB565(60,130,70));
static const uint16_t GOLD = PX(RGB565(245,188,76));
static const uint16_t SHADE = PX(RGB565(172,114,51));
static const uint16_t INK = PX(RGB565(39,43,58));
static const uint16_t BLUE = PX(RGB565(106,171,209));
static const uint16_t LIGHT = PX(RGB565(202,222,226));
static const uint16_t ROSE = PX(RGB565(217,119,87));
static const uint16_t PINK = PX(RGB565(242,157,185));
static const uint16_t CREAM = PX(RGB565(255,234,228));
static const uint16_t VIOLET = PX(RGB565(149,138,203));

// Keep a protected central torso even for extreme width/height combinations.
// The painter is local to draw(), so no shared mutable drawing context is needed.
struct Painter {
int protectLeft,protectRight,protectTop,protectBottom;
void rect(int x,int y,int w,int h,uint16_t color) const {
    int x0=x < 0 ? 0 : x, y0=y < 0 ? 0 : y;
    int x1=x+w > SCR_W ? SCR_W : x+w;
    int y1=y+h > SCR_H ? SCR_H : y+h;
    for(int py=y0;py<y1;++py)
        for(int px=x0;px<x1;++px) {
            if(px>=protectLeft && px<=protectRight && py>=protectTop && py<=protectBottom) continue;
            gfx::pixel(px,py,color);
        }
}
void box(int x,int y,int w,int h,int s,uint16_t color) const {
    rect(x,y,w,s,color); rect(x,y+h-s,w,s,color);
    rect(x,y,s,h,color); rect(x+w-s,y,s,h,color);
}
void bubble(int cx,int cy,int s,uint16_t color) const {
    rect(cx-2*s,cy-3*s,4*s,s,color);
    rect(cx-2*s,cy+2*s,4*s,s,color);
    rect(cx-3*s,cy-2*s,s,4*s,color);
    rect(cx+2*s,cy-2*s,s,4*s,color);
    rect(cx-s,cy-2*s,s,s,LIGHT);
}
void leaf(int cx,int top,int s) const {
    rect(cx,top-6*s,s,6*s,STEM);
    rect(cx+s,top-9*s,3*s,2*s,GREEN);
    rect(cx,top-8*s,3*s,3*s,GREEN);
    rect(cx+s,top-7*s,s,2*s,STEM);
}
void star(int cx,int cy,int diameter) const {
    static const int8_t points[10][2]={{0,-10},{3,-3},{10,-3},{5,2},{7,10},{0,5},{-7,10},{-5,2},{-10,-3},{-3,-3}};
    int vx[10],vy[10];
    for(int i=0;i<10;++i){vx[i]=cx+points[i][0]*diameter/20;vy[i]=cy+points[i][1]*diameter/20;}
    for(int y=cy-diameter/2;y<=cy+diameter/2;++y){
        int crossings[10],n=0;
        for(int i=0,j=9;i<10;j=i++) {
            if((vy[i]<=y && vy[j]>y)||(vy[j]<=y && vy[i]>y))
                crossings[n++]=vx[i]+(y-vy[i])*(vx[j]-vx[i])/(vy[j]-vy[i]);
        }
        for(int i=1;i<n;++i){int value=crossings[i],j=i;while(j && crossings[j-1]>value){crossings[j]=crossings[j-1];--j;}crossings[j]=value;}
        for(int i=0;i+1<n;i+=2)rect(crossings[i],y,crossings[i+1]-crossings[i]+1,1,GOLD);
    }
}
};

bool draw(uint8_t index,uint32_t elapsed_ms,int cx,int cy,int body_w,int body_h) {
    if (!validate(index) || !fb || body_w<=0 || body_h<=0 || body_w>1024 || body_h>1024) return false;
    if (cx < -2048 || cx > 2408 || cy < -2048 || cy > 2408) return true;
    int s=body_w/48;
    if(s<1)s=1;
    if(s>3)s=3;
    const int top=cy-body_h/2;
    const int right=cx+body_w/2;
    // The top cross-section of the actual forms is 50/72, 72/104,
    // 102/148 and 91/132 pixels: the arms make the full body much wider.
    // Fit headwear to that cross-section instead of using a scale-step sprite.
    const int headW=(body_w*69+50)/100;
    const int headHalf=headW/2;
    const int rim=(headW+12)/24 > 0 ? (headW+12)/24 : 1;
    const int eyeY=cy-(body_w<=80 ? body_h/3 : (body_h*32+50)/100);
    const int eyeDX=(body_w*(body_w<=80 ? 21 : 20)+50)/100;
    const int phase=(elapsed_ms%2000)/500;
    const Painter p{cx-body_w/4,cx+body_w/4,cy-body_h/4,cy+body_h/4};
    if(body_w<16 || body_h<16) {
        p.rect(cx,top-4,2,2,GOLD);
        return true;
    }
    switch(index) {
    case 0: {
        int diameter=(headW*34+50)/100;
        if(diameter<16)diameter=16;
        if(diameter>32)diameter=32;
        const int petal=diameter/3;
        const int stem=rim>1 ? rim-1 : 1;
        const int fy=top-diameter/2-6;
        p.rect(cx-stem/2,fy+petal,stem,top-fy-petal+1,STEM);
        p.rect(cx-stem-5,top-7,5,3,GREEN);
        p.rect(cx+stem,top-5,5,3,GREEN);
        p.rect(cx-petal/2,fy-diameter/2,petal,petal+1,PINK);
        p.rect(cx-petal/2,fy+diameter/2-petal,petal,petal+1,PINK);
        p.rect(cx-diameter/2,fy-petal/2,petal+1,petal,CREAM);
        p.rect(cx+diameter/2-petal,fy-petal/2,petal+1,petal,CREAM);
        p.rect(cx-petal/2,fy-petal/2,petal,petal,GOLD);
        break;
    }
    case 1: {
        const int height=(body_w*20+50)/100;
        const int crown=(headW*89+50)/100;
        const int step=height/5 > 0 ? height/5 : 1;
        const int inset=headW/10;
        p.rect(cx-crown/2+inset,top-height,crown-2*inset,step,GOLD);
        p.rect(cx-crown/2+inset/2,top-height+step,crown-inset,step,GOLD);
        p.rect(cx-crown/2,top-height+2*step,crown,height-2*step,GOLD);
        p.rect(cx-headHalf-rim,top-1,headW+2*rim,rim,SHADE);
        p.rect(cx-(rim+1)/2,top-height+1,rim+1,height-2,LIGHT);
        break;
    }
    case 2: {
        const int padW=(headW*11+50)/100 > 1 ? (headW*11+50)/100 : 2;
        const int padH=(body_h*25+50)/100 > 2 ? (body_h*25+50)/100 : 3;
        const int padTop=eyeY-padH/2;
        p.rect(cx-headHalf,top-2*rim,headW,rim,INK);
        p.rect(cx-headHalf-rim,top-rim,rim,padTop-top+padH/2+rim,INK);
        p.rect(cx+headHalf,top-rim,rim,padTop-top+padH/2+rim,INK);
        p.rect(cx-headHalf-padW+rim/2,padTop,padW,padH,BLUE);
        p.rect(cx+headHalf-rim/2,padTop,padW,padH,BLUE);
        p.rect(cx-headHalf-padW+rim/2,padTop+rim,rim/2+1,padH-2*rim,LIGHT);
        p.rect(cx+headHalf-rim/2+padW-rim/2-1,padTop+rim,rim/2+1,padH-2*rim,LIGHT);
        break;
    }
    case 3: {
        const int width=(headW*45+50)/100;
        const int height=(headW*26+50)/100;
        const int bx=cx+headW/5, by=top-height-6;
        p.rect(cx+rim,top-2*rim,rim,rim,LIGHT);
        p.rect(cx+3*rim,top-4*rim,rim+1,rim+1,LIGHT);
        p.rect(bx-width/2,by-height/2+rim,width,height-2*rim,LIGHT);
        p.rect(bx-width/2+rim,by-height/2,width-2*rim,height,LIGHT);
        if(phase&1)p.rect(bx+rim,by-rim,rim,rim,BLUE);
        break;
    }
    case 4: {
        const int rise=phase*rim;
        p.bubble(right+4*rim,top+4*rim-rise,rim,BLUE);
        p.bubble(right+rim,top-7*rim-rise,rim>1 ? rim-1 : 1,LIGHT);
        break;
    }
    case 5: {
        const int height=(body_w*23+50)/100;
        p.rect(cx-headHalf+rim,top-height/2,headW-2*rim,height/2,BLUE);
        p.rect(cx-headHalf+2*rim,top-height*3/4,headW-4*rim,height/4+1,BLUE);
        p.rect(cx-headHalf+4*rim,top-height,headW/2,height/4+1,BLUE);
        p.rect(cx+headW/4,top-height,headW/3,rim,BLUE);
        p.rect(cx+headHalf-rim,top-height+rim,rim,height/3,BLUE);
        p.rect(cx+headHalf-2*rim,top-height+height/3,3*rim,3*rim,LIGHT);
        p.rect(cx-headHalf-rim,top-1,headW+2*rim,rim,LIGHT);
        p.rect(right+s,top-8*s-(phase&1)*s,3*s,s,LIGHT);
        p.rect(right+2*s,top-7*s-(phase&1)*s,s,s,LIGHT);
        p.rect(right+s,top-6*s-(phase&1)*s,3*s,s,LIGHT);
        break;
    }
    case 6: {
        const int lensW=(body_w*16+50)/100;
        const int lensH=(body_h*15+50)/100;
        const int thick=(headW+18)/36 > 0 ? (headW+18)/36 : 1;
        const int gy=eyeY-lensH/2;
        p.box(cx-eyeDX-lensW/2,gy,lensW,lensH,thick,INK);
        p.box(cx+eyeDX-lensW/2,gy,lensW,lensH,thick,INK);
        p.rect(cx-eyeDX+ lensW-lensW/2,eyeY-thick/2,eyeDX*2-lensW,thick,INK);
        break;
    }
    case 7: {
        const int x=right-s, y=cy+body_h/5;
        p.rect(x,y,8*s,8*s,BLUE);
        p.rect(x+2*s,y+s,5*s,6*s,LIGHT);
        p.rect(x+4*s,y+s,s,6*s,SHADE);
        p.rect(x,y,2*s,8*s,INK);
        break;
    }
    case 8: {
        const int x=right-s, y=cy+body_h/6;
        p.rect(right,top+body_h/3,s,body_h/3,SHADE);
        p.rect(x,y,7*s,6*s,SHADE);
        p.rect(x+s,y+s,5*s,4*s,GOLD);
        p.rect(x+3*s,y+s,s,s,LIGHT);
        break;
    }
    case 9: {
        const int rawWidth=(headW*40+50)/100;
        const int width=rawWidth<18 ? 18 : rawWidth;
        const int height=(width*60+50)/100;
        const int x=cx-headW/3, y=top-height/2;
        const int knot=width/5 > 0 ? width/5 : 1;
        p.rect(x-width/2,y-height/2,width/4,height,ROSE);
        p.rect(x-width/4,y-height/4,width/4-knot/2,height/2+1,ROSE);
        p.rect(x+(knot+1)/2,y-height/4,width/4-knot/2,height/2+1,ROSE);
        p.rect(x+width/4,y-height/2,width/4,height,ROSE);
        p.rect(x-knot/2,y-knot/2,knot,knot,GOLD);
        break;
    }
    case 10: {
        const int height=(body_w*20+50)/100;
        const int crownW=headW+2*rim;
        p.rect(cx-crownW/2,top-height/2,crownW,height/2+rim,GOLD);
        p.rect(cx-crownW/2,top-height,rim*3,height/2,GOLD);
        p.rect(cx-rim*2,top-height-rim,rim*4,height/2+rim,GOLD);
        p.rect(cx+crownW/2-rim*3,top-height,rim*3,height/2,GOLD);
        p.rect(cx-crownW/2,top,crownW,rim,SHADE);
        p.rect(cx-rim,top-height/3,2*rim,2*rim,ROSE);
        break;
    }
    case 11: {
        const int height=(body_w*17+50)/100;
        p.rect(cx-headHalf-rim,top-height/2,headW+rim*2,height/2,ROSE);
        p.rect(cx-headHalf,top-height+rim,headW-rim,height/2,ROSE);
        p.rect(cx-headHalf+headW/5,top-height,headW/2,rim,ROSE);
        p.rect(cx-rim,top-height-2*rim,rim,2*rim,SHADE);
        p.rect(cx-headHalf,top-1,headW,rim,SHADE);
        break;
    }
    case 12: {
        const int width=(headW*75+50)/100;
        const int height=width/4;
        const int y=top-height-7;
        p.rect(cx-width/2+rim,y,width-2*rim,rim,GOLD);
        p.rect(cx-width/2+rim,y+height-rim,width-2*rim,rim,GOLD);
        p.rect(cx-width/2,y+rim,rim,height-2*rim,GOLD);
        p.rect(cx+width/2-rim,y+rim,rim,height-2*rim,GOLD);
        p.rect(cx-width/4,y+rim,rim,rim,CREAM);
        break;
    }
    case 13: {
        const int raw=(headW*40+50)/100;
        const int diameter=raw<18 ? 18 : raw;
        const int half=diameter/2;
        const int x=cx-headW/3, y=top-half+rim;
        p.star(x,y,diameter);
        p.rect(x-rim/2,y-rim/2,rim,rim,CREAM);
        break;
    }
    case 14: {
        const int height=(body_w*34+50)/100;
        const int base=(headW*88+50)/100;
        const int step=rim>1 ? rim : 2;
        for(int y=0;y<height;y+=step) {
            int width=base*(y+step)/height;
            if(width<step)width=step;
            p.rect(cx-width/2,top-height+y,width,step,VIOLET);
        }
        p.rect(cx-headHalf-rim,top-1,headW+2*rim,rim,BLUE);
        p.rect(cx-rim,top-height/2-rim,2*rim,3*rim,GOLD);
        p.rect(cx-2*rim,top-height/2,4*rim,rim,GOLD);
        break;
    }
    case 15: {
        const int raw=(headW*40+50)/100;
        const int width=raw<18 ? 18 : raw;
        const int height=width*4/5;
        const int x=cx-headW/3, y=top-height+rim;
        p.rect(x-width/2,y+height/5,width,height*2/5,PINK);
        p.rect(x-width/2+rim,y,width/2-2*rim,height/3,PINK);
        p.rect(x+rim,y,width/2-2*rim,height/3,PINK);
        for(int row=height*3/5;row<height;row+=rim) {
            const int span=width*(height-row)/(height*2/5);
            p.rect(x-span/2,y+row,span,rim,PINK);
        }
        break;
    }
    case 16: {
        const int height=(body_w*20+50)/100;
        p.rect(cx-headHalf+rim,top-height/2,headW-2*rim,height/2,BLUE);
        p.rect(cx-headHalf+3*rim,top-height+rim,headW-6*rim,height/2,BLUE);
        p.rect(cx-headW/4,top-height,headW/2,rim,BLUE);
        p.rect(cx-headHalf-rim,top-1,headW+4*rim,rim,INK);
        p.rect(cx-rim,top-height/2,2*rim,2*rim,GOLD);
        break;
    }
    case 17: {
        const int padH=(body_h*33+50)/100;
        const int padW=padH*3/4;
        const int y=eyeY-padH/2;
        p.rect(cx-headHalf,top-2*rim,headW,rim,ROSE);
        p.rect(cx-headHalf-rim,top-rim,rim,eyeY-top+rim,ROSE);
        p.rect(cx+headHalf,top-rim,rim,eyeY-top+rim,ROSE);
        for(int side=-1;side<=1;side+=2) {
            const int x=cx+side*(headHalf+padW/3)-padW/2;
            p.rect(x+rim,y,padW-2*rim,padH,PINK);
            p.rect(x,y+rim,padW,padH-2*rim,PINK);
            p.rect(x+rim,y+rim,rim,padH-2*rim,CREAM);
        }
        break;
    }
    case 18: {
        const uint16_t colors[3]={ROSE,GOLD,GREEN};
        for(int band=0;band<3;++band) {
            const int rx=headW*40/100-band*rim;
            const int ry=headW*22/100-band*rim;
            const int ix=rx-rim>0 ? rx-rim : 1;
            const int iy=ry-rim>0 ? ry-rim : 1;
            for(int y=-ry;y<=0;++y)for(int x=-rx;x<=rx;++x)
                if(int64_t(x)*x*ry*ry+int64_t(y)*y*rx*rx<=int64_t(rx)*rx*ry*ry &&
                   int64_t(x)*x*iy*iy+int64_t(y)*y*ix*ix>=int64_t(ix)*ix*iy*iy)
                    p.rect(cx+x,top-5+y,1,1,colors[band]);
        }
        break;
    }
    case 19: {
        const int height=(body_w*18+50)/100;
        const int tip=2*rim+1;
        for(int side=-1;side<=1;side+=2) {
            const int x=cx+side*headW/4;
            p.rect(x-rim/2,top-height,rim,height,STEM);
            p.rect(x-tip/2,top-height-tip/2,tip,tip,side<0 ? GOLD : BLUE);
            p.rect(x-tip/2,top-height-tip/2,tip,rim,LIGHT);
        }
        break;
    }
    default: return false;
    }
    return true;
}
} // namespace outfits
