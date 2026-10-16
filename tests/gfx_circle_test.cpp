#include "../system/gfx.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr uint16_t GUARD=0x7e31;
static constexpr uint16_t BACKGROUND=0x10c4;
static constexpr uint16_t INK=0xd977;
static std::vector<uint16_t> framebuffer(SCR_W*SCR_H+2,GUARD);

// Pixel compatibility oracle: the previous visible predicate, sampled over the
// entire screen independently of strokeCircle's bounding-box/clipping code.
static void check(float cx,float cy,float radius,float thickness) {
    std::fill(fb,fb+SCR_W*SCR_H,BACKGROUND);
    gfx::strokeCircle(cx,cy,radius,thickness,INK);
    const float inner=radius-thickness*.5f;
    const float outer=radius+thickness*.5f;
    const float outerSquared=outer*outer;
    for(int y=0;y<SCR_H;++y)for(int x=0;x<SCR_W;++x) {
        const float dx=(x+.5f)-cx,dy=(y+.5f)-cy;
        const float distanceSquared=dx*dx+dy*dy;
        const bool painted=distanceSquared<=outerSquared && std::sqrt(distanceSquared)>=inner;
        const uint16_t expected=painted?INK:BACKGROUND;
        if(fb[y*SCR_W+x]!=expected) {
            std::fprintf(stderr,"circle mismatch: center=(%.4f,%.4f) r=%.4f thickness=%.4f pixel=(%d,%d)\n",
                         cx,cy,radius,thickness,x,y);
            assert(false);
        }
    }
    assert(framebuffer.front()==GUARD && framebuffer.back()==GUARD);
}

int main() {
    fb=framebuffer.data()+1;
    unsigned cases=0;
    // The six care buttons, using the same fractional coordinates as GamePet.
    for(int i=0;i<6;++i) {
        const float step=110.0f/5.0f;
        const float angle=(-110.0f*.5f+step*i)*PI_F/180.0f;
        check(SCR_CX+std::sin(angle)*134.0f,SCR_CY+std::cos(angle)*134.0f,24.0f*.82f,1.0f);
        ++cases;
    }
    check(37,202,20,1);check(323,202,20,1);cases+=2;
    const float centers[][2]={{180,180},{180.25f,179.75f},{0,0},{359,359},
                               {-5.25f,2.5f},{362,180},{-60,-60},{120.5f,240.5f}};
    for(auto& center:centers)
        for(float radius:{0.0f,.25f,1.0f,3.5f,9.0f,19.68f,20.0f,42.5f})
            for(float thickness:{.25f,1.0f,2.0f,2.5f,8.0f,50.0f}) {
                check(center[0],center[1],radius,thickness);
                ++cases;
            }
    std::printf("gfx_circle PASS: %u cases match previous visible pixels; care buttons, thick strokes and clipping verified\n",cases);
}
