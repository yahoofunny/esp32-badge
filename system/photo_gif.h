#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "gfx.h"

namespace photo {
// Keep rendered RGB colours, rather than palette indices: GIF frames can each
// supply a different palette. Disposal belongs to the PREVIOUS frame.
class GifCanvas {
public:
    void begin(int width,int height,uint16_t *pixels,uint16_t *saved) {
        w=width;h=height;canvas=pixels;restore=saved;previous={};
        reset();
    }
    void reset() {
        if(canvas)std::memset(canvas,0,size_t(w)*h*2);
        previous={};
    }
    void frame(int x,int y,int width,int height,int disposal) {
        if(previous.disposal==2)clear(previous);
        else if(previous.disposal==3&&restore)
            std::memcpy(canvas,restore,size_t(w)*h*2);
        if(disposal==3&&restore)std::memcpy(restore,canvas,size_t(w)*h*2);
        previous={x,y,width,height,disposal};
    }
    void line(int x,int y,int width,const uint8_t *indices,const uint16_t *palette,
              bool transparent,uint8_t transparentIndex) {
        if(!canvas||y<0||y>=h)return;
        const int first=std::max(0,x),last=std::min(w,x+width);
        auto *out=canvas+size_t(y)*w;
        for(int X=first;X<last;++X) {
            const uint8_t index=indices[X-x];
            if(!transparent||index!=transparentIndex)out[X]=palette[index];
        }
    }
private:
    struct Rect {int x=0,y=0,w=0,h=0,disposal=0;};
    int w=0,h=0;
    uint16_t *canvas=nullptr,*restore=nullptr;
    Rect previous;
    void clear(const Rect &r) {
        const int x0=std::max(0,r.x),x1=std::min(w,r.x+r.w);
        if(x1<=x0)return;
        for(int y=std::max(0,r.y);y<std::min(h,r.y+r.h);++y)
            std::memset(canvas+size_t(y)*w+x0,0,size_t(x1-x0)*2);
    }
};

// Build coordinate lookups once when opening/toggling a GIF. The old scanline
// scaler divided 64-bit integers for every destination pixel on every frame.
class GifView {
public:
    void begin(int w,int h,int width,int height,int left,int top) {
        sourceW=w;
        for(int x=0;x<SCR_W;++x) {
            const int sx=floorDivide((x-left)*w,width);
            xs[x]=(sx>=0&&sx<w)?sx:-1;
        }
        for(int y=0;y<SCR_H;++y) {
            const int sy=floorDivide((y-top)*h,height);
            ys[y]=(sy>=0&&sy<h)?sy:-1;
        }
    }
    void beginContent(int w,int h,int x0,int y0,int cw,int ch) {
        // Rational coordinates avoid host/ESP floating-point rounding differences
        // exactly at a source-pixel boundary. Limit body size and the full scene.
        int num=160,den=102;
        if(270*den<num*cw){num=270;den=cw;}
        if(260*den<num*ch){num=260;den=ch;}
        sourceW=w;
        for(int x=0;x<SCR_W;++x) {
            const int sx=floorDivide(2*(x-180)*den+(2*x0+cw)*num,2*num);
            xs[x]=(sx>=0&&sx<w)?sx:-1;
        }
        for(int y=0;y<SCR_H;++y) {
            const int sy=floorDivide(2*(y-174)*den+(2*y0+ch)*num,2*num);
            ys[y]=(sy>=0&&sy<h)?sy:-1;
        }
    }
    void draw(uint16_t *out,const uint16_t *canvas) const {
        for(int y=0;y<SCR_H;++y) {
            auto *dest=out+size_t(y)*SCR_W;
            if(ys[y]<0){std::memset(dest,0,SCR_W*2);continue;}
            const auto *src=canvas+size_t(ys[y])*sourceW;
            for(int x=0;x<SCR_W;++x)dest[x]=xs[x]<0?0:PX(src[xs[x]]);
        }
    }
private:
    static int floorDivide(int a,int b){return a>=0?a/b:-((-a+b-1)/b);}
    int sourceW=0;
    int16_t xs[SCR_W]={},ys[SCR_H]={};
};
} // namespace photo
