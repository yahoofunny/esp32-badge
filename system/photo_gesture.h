#pragma once
#include <stdint.h>

namespace photo {
struct GestureAction {int step=0;bool toggleFit=false;};
class Gesture {
    bool tracking=false,moved=false,held=false;
    int startX=0,startY=0;
public:
    void reset(){tracking=moved=held=false;}
    GestureAction update(bool down,bool released,int x,int y,uint32_t heldMs) {
        GestureAction action;
        if(down) {
            if(!tracking){tracking=true;moved=held=false;startX=x;startY=y;}
            const int dx=x-startX,dy=y-startY;
            if(dx*dx+dy*dy>14*14)moved=true;
            if(!moved && !held && heldMs>=650){held=true;action.toggleFit=true;}
        } else if(tracking) {
            if(released && !held) {
                const int dx=x-startX,dy=y-startY;
                const int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
                if(ax>=36 && ax*4>=ay*5)action.step=dx<0?1:-1;
                else if(!moved)action.step=x<180?-1:1;
            }
            reset();
        }
        return action;
    }
};
}
