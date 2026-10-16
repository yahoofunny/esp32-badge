#include "../firmware/clawd_pet/pet_ui_pacing.h"
#include <cassert>
#include <cstdio>
#include <limits>

using clawd::UiPacing;
static_assert(UiPacing::kInputPollMs == 20);

int main() {
    {
        UiPacing p;
        assert(p.renderDue(123,false,false,false,false));
        assert(!p.renderDue(143,false,false,false,false));
        assert(!p.renderDue(202,false,false,false,false));
        assert(p.renderDue(203,false,false,false,false));
        assert(!p.renderDue(203,false,false,false,false));
    }
    {
        UiPacing p;
        assert(p.renderDue(0,false,false,false,false));
        assert(p.renderDue(25,true,true,false,false)); // press bypasses idle period
        assert(!p.renderDue(45,true,false,false,false));
        assert(p.renderDue(65,true,false,false,false));
        assert(p.renderDue(70,false,true,false,false)); // release bypasses 40 ms
        assert(!p.renderDue(90,false,false,false,false));
        assert(p.renderDue(110,false,false,false,false));
        assert(p.renderDue(350,false,false,false,false));
        assert(!p.renderDue(370,false,false,false,false)); // touch tail expired
        assert(p.renderDue(430,false,false,false,false)); // idle is now 80 ms
    }
    {
        UiPacing p;
        assert(p.renderDue(0,false,false,true,false));
        assert(!p.renderDue(80,false,false,true,false));
        assert(p.renderDue(100,false,false,true,false));
        assert(p.renderDue(120,true,true,true,true));
        assert(p.renderDue(160,true,false,true,true)); // touching remains responsive
        assert(p.renderDue(180,false,true,true,true));
        assert(p.renderDue(460,false,false,true,true));
        assert(!p.renderDue(500,false,false,true,true));
        assert(p.renderDue(560,false,false,true,true)); // speech wins over sleep
    }
    {
        UiPacing p;
        int renders=0;
        for(uint32_t now=0;now<=1000;now+=UiPacing::kInputPollMs)
            if(p.renderDue(now,false,false,false,true))++renders;
        assert(renders==3); // first frame, 500 ms, 1000 ms
    }
    {
        // A short press fits between two sleeping draw frames, yet the separate
        // input scan sees both edges and requests immediate visual responses.
        UiPacing p;
        bool previous=false;
        int presses=0,releases=0,initialSleepRenders=0;
        for(uint32_t now=0;now<=1000;now+=UiPacing::kInputPollMs) {
            const bool down=now>=220 && now<280;
            const bool event=down!=previous;
            const bool render=p.renderDue(now,down,event,false,true);
            if(now<220 && render)++initialSleepRenders;
            if(event) {
                assert(render);
                if(down)++presses;else ++releases;
            }
            previous=down;
        }
        assert(initialSleepRenders==1 && presses==1 && releases==1);
    }
    {
        UiPacing p;
        assert(p.renderDue(100,false,false,false,false));
        assert(p.renderDue(199,false,false,false,false)); // 19 ms late
        assert(!p.renderDue(259,false,false,false,false));
        assert(p.renderDue(260,false,false,false,false)); // cadence remains anchored
        assert(p.renderDue(9999,false,false,false,false)); // skip many stale frames
        assert(!p.renderDue(9999,false,false,false,false)); // no catch-up burst
        assert(!p.renderDue(10019,false,false,false,false));
        assert(p.renderDue(10020,false,false,false,false));
    }
    {
        const uint32_t start=std::numeric_limits<uint32_t>::max()-15;
        UiPacing p;
        assert(p.renderDue(start,false,false,false,false));
        assert(!p.renderDue(start+20,false,false,false,false));
        assert(p.renderDue(start+80,false,false,false,false));
        UiPacing touch;
        assert(touch.renderDue(start,true,true,false,true));
        assert(!touch.renderDue(start+20,true,false,false,true));
        assert(touch.renderDue(start+40,true,false,false,true));
        assert(touch.renderDue(start+60,false,true,false,true));
        assert(touch.renderDue(start+100,false,false,false,true));
        assert(touch.renderDue(start+340,false,false,false,true));
        assert(!touch.renderDue(start+360,false,false,false,true));
        assert(touch.renderDue(start+840,false,false,false,true));
    }
    puts("PASS: fixed input polling, first frame, immediate touch edges, active/idle/voice/sleep pacing, no cadence drift, millis wrap");
}
