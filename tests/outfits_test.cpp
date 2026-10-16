#include "../system/outfits.h"
#include "../system/gfx.h"
#include "../system/assets_mon.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <limits>
#include <vector>

static constexpr uint16_t GUARD=0x12ab;
static std::vector<uint16_t> storage(SCR_W*SCR_H+2,GUARD);
static uint8_t activeAccessory=0;
static void checkGuards() {
    assert(storage.front()==GUARD && storage.back()==GUARD);
}
static void checkBodyUntouched(const std::vector<uint16_t>& before,int cx,int cy,int w,int h) {
    // An accessory must retain every pixel in the original body's central torso.
    for(int y=std::max(0,cy-h/4);y<=std::min(SCR_H-1,cy+h/4);++y)
        for(int x=std::max(0,cx-w/4);x<=std::min(SCR_W-1,cx+w/4);++x)
            if(fb[y*SCR_W+x]!=before[y*SCR_W+x]) {
                fprintf(stderr,"Body changed: accessory=%u (%s), center=%d,%d size=%dx%d pixel=%d,%d\n",
                    activeAccessory,outfits::name(activeAccessory),cx,cy,w,h,x,y);
                assert(false);
            }
}
static void drawOriginalBody(int width) {
    const auto& f=mon_forms[0];
    gfx::Sprite sprite{f.bits,f.w,f.h,f.rowBytes};
    const uint16_t palette[]={0,PX(f.body),PX(f.edge),PX(f.ink)};
    gfx::drawSpriteSized(sprite,180,180,0,width,palette);
}

int main() {
    fb=storage.data()+1;
    const auto bg=PX(0x10c4);
    const uint8_t total=outfits::count();
    assert(total>0 && total<255);
    assert(outfits::resourceBytes()<64*1024);
    const int rows=(total+1+3)/4;
    std::vector<uint16_t> sheet(720*rows*180,bg);
    unsigned rendered=0;
    for(uint8_t accessory=0;accessory<total;++accessory) {
        activeAccessory=accessory;
        assert(outfits::name(accessory)[0] && outfits::duration(accessory)>0);
        assert(outfits::validate(accessory));
        for(uint32_t time=0;time<outfits::duration(accessory);time+=125) {
            // A patterned region catches body replacement even in the same color.
            for(int y=0;y<SCR_H;++y)for(int x=0;x<SCR_W;++x)
                fb[y*SCR_W+x]=uint16_t((x*53+y*137)^0xa5a5);
            const std::vector<uint16_t> before(fb,fb+SCR_W*SCR_H);
            assert(outfits::draw(accessory,time,180,180,96,72));
            assert(!std::equal(before.begin(),before.end(),fb));
            checkBodyUntouched(before,180,180,96,72);
            assert(fb[0]==before[0] && fb[SCR_W*SCR_H-1]==before.back());
            checkGuards();++rendered;
        }
        for(auto time:{outfits::duration(accessory)-1,outfits::duration(accessory),
                      std::numeric_limits<uint32_t>::max()}) {
            assert(outfits::draw(accessory,time,180,180,96,72));checkGuards();
        }
        // Protect the original torso at every real form size and the egg size.
        for(int form=0;form<=MON_FORM_COUNT;++form) {
            const int width=form<MON_FORM_COUNT?mon_forms[form].w:68;
            const int height=form<MON_FORM_COUNT?mon_forms[form].h:88;
            for(uint32_t time=0;time<outfits::duration(accessory);time+=125) {
                std::fill(fb,fb+SCR_W*SCR_H,bg);
                const std::vector<uint16_t> before(fb,fb+SCR_W*SCR_H);
                assert(outfits::draw(accessory,time,180,184,width,height));
                checkBodyUntouched(before,180,184,width,height);checkGuards();
            }
        }
        // Arbitrary aspect ratios exercise clipping only; the actual form sizes
        // above define how the accessory fits the pet's unchanged proportions.
        for(auto width:{1,17,80,140,1024})
            for(auto height:{1,17,80,140,1024})
                for(auto x:{-500,-10,0,359,500})
                    for(auto y:{-500,-10,0,359,500}) {
                        std::fill(fb,fb+SCR_W*SCR_H,bg);
                        assert(outfits::draw(accessory,0,x,y,width,height));
                        checkGuards();
                    }
    }
    // The same actual sprite, once plain and once with each accessory.
    const int bodyWidth=96;
    const int bodyHeight=(int(mon_forms[0].h)*bodyWidth+mon_forms[0].w/2)/mon_forms[0].w;
    for(int style=0;style<=total;++style) {
        gfx::clear(bg);drawOriginalBody(bodyWidth);
        const std::vector<uint16_t> before(fb,fb+SCR_W*SCR_H);
        if(style) {
            activeAccessory=style-1;
            assert(outfits::draw(style-1,0,180,180,bodyWidth,bodyHeight));
            checkBodyUntouched(before,180,180,bodyWidth,bodyHeight);
        }
        gfx::textCentered(style?outfits::name(style-1):"ORIGINAL",258,PX(0xffff));
        for(int y=0;y<180;++y)for(int x=0;x<180;++x)
            sheet[((style/4)*180+y)*720+(style%4)*180+x]=fb[(90+y)*SCR_W+90+x];
    }
    for(auto invalid:{total,uint8_t(255)}) {
        assert(!outfits::validate(invalid) && !outfits::draw(invalid,0,180,180,96,72));
        assert(outfits::duration(invalid)==0 && outfits::name(invalid)[0]==0);
    }
    for(auto invalid:{-1,0,1025}) {
        assert(!outfits::draw(0,0,180,180,invalid,72));
        assert(!outfits::draw(0,0,180,180,96,invalid));
    }
    auto realFb=fb;fb=nullptr;
    assert(!outfits::draw(0,0,180,180,96,72));fb=realFb;
    std::ofstream preview("build/outfits-render.ppm",std::ios::binary);
    preview<<"P6\n720 "<<rows*180<<"\n255\n";
    for(auto pixel:sheet) {
        uint16_t color=PX(pixel);
        char rgb[]={char(((color>>11)&31)*255/31),char(((color>>5)&63)*255/63),char((color&31)*255/31)};
        preview.write(rgb,3);
    }
    printf("outfits PASS: %u accessories, %u sampled frames, %u flash bytes; original torso, transparency and clipping verified\n",
           total,rendered,outfits::resourceBytes());
}
