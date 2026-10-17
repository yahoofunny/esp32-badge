#include <cassert>
#include <cstdio>
#include "../system/photo_gesture.h"

int main() {
    photo::Gesture g;
    auto result=g.update(true,false,230,180,0);assert(!result.step && !result.toggleFit);
    result=g.update(false,true,230,180,0);assert(result.step==1 && !result.toggleFit);
    g.update(true,false,100,180,0);
    assert(g.update(false,true,100,180,0).step==-1);
    g.update(true,false,250,180,0);g.update(true,false,120,183,400);
    result=g.update(false,true,120,183,0);assert(result.step==1 && !result.toggleFit);
    g.update(true,false,100,180,0);g.update(true,false,250,180,700);
    assert(g.update(false,true,250,180,0).step==-1);
    // The existing hold gesture still toggles fill/fit once, never changes the photo.
    g.update(true,false,180,180,0);
    result=g.update(true,false,180,180,649);assert(!result.toggleFit && !result.step);
    result=g.update(true,false,180,180,650);assert(result.toggleFit && !result.step);
    for(int i=0;i<5;++i)assert(!g.update(true,false,180,180,800+i*100).toggleFit);
    result=g.update(false,true,180,180,0);assert(!result.step && !result.toggleFit);
    // A moving finger cannot become a long press, and vertical drags don't page.
    g.update(true,false,180,100,0);
    result=g.update(true,false,183,240,1000);assert(!result.toggleFit);
    assert(!g.update(false,true,183,240,0).step);
    // A short drag is cancelled rather than interpreted as an opposite-side tap.
    g.update(true,false,181,180,0);g.update(true,false,157,180,100);
    assert(!g.update(false,true,157,180,0).step);
    g.update(true,false,150,180,0);g.reset();
    assert(!g.update(false,true,150,180,0).step);
    puts("PASS: photo left/right tap and swipe, vertical/short drag rejection, original 650ms fill/fit hold, no paging on hold release");
}
