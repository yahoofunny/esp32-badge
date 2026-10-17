#include "../system/photo_gif.h"
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    uint16_t pixels[12],saved[12],palette[4]={0x07e0,0xf800,0x001f,0xffff};
    uint8_t first[4]={0,1,0,2};
    photo::GifCanvas canvas;canvas.begin(4,3,pixels,saved);
    canvas.frame(0,0,4,1,2);
    canvas.line(0,0,4,first,palette,true,0);
    assert(pixels[0]==0&&pixels[1]==0xf800&&pixels[2]==0&&pixels[3]==0x001f);
    // Previous disposal=2 clears its old rectangle, even when the next frame
    // occupies a different, smaller rectangle.
    canvas.frame(1,1,1,1,1);
    uint8_t blue=2;canvas.line(1,1,1,&blue,palette,false,0);
    assert(pixels[1]==0&&pixels[3]==0&&pixels[5]==0x001f);
    // A new local palette must not recolour the old frame's retained pixels.
    palette[2]=0xffe0;
    canvas.frame(2,1,1,1,3);canvas.line(2,1,1,&blue,palette,false,0);
    assert(pixels[5]==0x001f&&pixels[6]==0xffe0);
    canvas.frame(3,2,1,1,1);
    assert(pixels[5]==0x001f&&pixels[6]==0); // previous disposal=3 restores
    // Negative/out-of-bounds frame rectangles cannot overwrite adjacent memory.
    uint8_t row[4]={1,1,1,1};canvas.line(-2,2,4,row,palette,false,0);
    assert(pixels[8]==0xf800&&pixels[9]==0xf800&&pixels[10]==0);
    canvas.reset();for(auto p:pixels)assert(p==0);

    std::vector<uint16_t> output(SCR_W*SCR_H+2,0xdead);
    uint16_t pattern[4]={0xf800,0x07e0,0x001f,0xffff};
    photo::GifView view;view.begin(2,2,100,100,130,130);
    view.draw(output.data()+1,pattern);
    assert(output.front()==0xdead&&output.back()==0xdead);
    auto at=[&](int x,int y){return output[1+y*SCR_W+x];};
    assert(at(129,130)==0&&at(230,180)==0&&at(130,129)==0);
    assert(at(130,130)==PX(0xf800)&&at(180,130)==PX(0x07e0));
    assert(at(130,180)==PX(0x001f)&&at(180,180)==PX(0xffff));
    std::puts("PASS: GIF transparency, previous-frame disposal 2/3, changing palettes, bounds and scaled colour order");
}
