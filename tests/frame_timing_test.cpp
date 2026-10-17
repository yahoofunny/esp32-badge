#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "../build/frame_timing_functions.h"
int main(){
    double elapsed=0;uint32_t previous=0;
    // Ten thousand iterations with only one second of real clock time. Skipped
    // LCD frames must not advance a snake/ball by thousands of invented frames.
    for(uint32_t i=1;i<=10000;++i){
        const uint32_t now=i/10;elapsed+=frameSeconds(now-previous);previous=now;
    }
    assert(std::abs(elapsed-1.0)<.00001);
    assert(frameSeconds(0)==0&&std::abs(frameSeconds(33)-.033f)<1e-6);
    const uint32_t before=0xfffffff0,after=17;
    assert(std::abs(frameSeconds(uint32_t(after-before))-.033f)<1e-6);
    assert(frameSeconds(1000)>0&&frameSeconds(1000)<.1f);
    puts("PASS: skipped-render loop iterations preserve real elapsed time, including same-millisecond calls, long stalls and millis wrap");
}
