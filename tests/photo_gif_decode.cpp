// Exercise the production compositor with the actual AnimatedGIF decoder.
#include "../system/photo_gif.h"
#include <AnimatedGIF.h>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdlib>
#include <cstdio>

static photo::GifCanvas canvas;
static std::vector<uint16_t> pixels,saved;
static void draw(GIFDRAW *d) {
    if(d->y==0)canvas.frame(d->iX,d->iY,d->iWidth,d->iHeight,d->ucDisposalMethod);
    canvas.line(d->iX,d->iY+d->y,d->iWidth,d->pPixels,d->pPalette,
                d->ucHasTransparency,d->ucTransparent);
}
int main(int argc,char **argv) {
    if(argc!=3&&argc!=7)return 2;
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});
    AnimatedGIF gif;gif.begin(LITTLE_ENDIAN_PIXELS);
    if(!gif.open(data.data(),int(data.size()),draw))return 3;
    const int w=gif.getCanvasWidth(),h=gif.getCanvasHeight();
    pixels.resize(w*h);saved.resize(w*h);canvas.begin(w,h,pixels.data(),saved.data());
    gif.setDrawType(GIF_DRAW_RAW);
    photo::GifView view;
    std::vector<uint16_t> screen(SCR_W*SCR_H);
    if(argc==7) {
        view.beginContent(w,h,std::atoi(argv[3]),std::atoi(argv[4]),
                          std::atoi(argv[5]),std::atoi(argv[6]));
    }
    std::ofstream output(argv[2],std::ios::binary);
    int frames=0,delay=0;
    auto writeFrame=[&]() {
        if(argc==7){view.draw(screen.data(),pixels.data());output.write((const char*)screen.data(),screen.size()*2);}
        else output.write((const char*)pixels.data(),pixels.size()*2);
        ++frames;
    };
    while(gif.playFrame(false,&delay)) {
        writeFrame();
    }
    // playFrame returns zero on the last successfully rendered frame as well.
    if(delay>0)writeFrame();
    gif.close();
    std::printf("%dx%d %d frames\n",w,h,frames);
}
