#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <cstdio>
using boolean=bool;
enum {CODEC_MP3,CODEC_AAC,CODEC_M4A,CODEC_FLAC};
static uint32_t frameBitrate=1800000;
static int FLACGetBitRate(){return frameBitrate;}
static int MP3GetBitrate(){return 128000;}
static int AACGetBitrate(){return 128000;}
static void (*audio_info)(const char*)=nullptr;
struct Audio {
    int m_codec=CODEC_FLAC;
    uint32_t m_validSamples=0,m_avr_bitrate=0,m_flacSampleRate=48000;
    uint32_t m_flacTotalSamplesInStream=48000*249,m_audioDataSize=54385002,m_audioDataStart=8780;
    float m_audioCurrentTime=0;
    int bitrate=0;
    void setBitrate(int value){bitrate=value;}
    int getBitRate(){return bitrate;}
    uint32_t getSampleRate(){return m_flacSampleRate;}
    uint32_t getFilePos(){return 40000000;}
    uint32_t inBufferFilled(){return 128000;}
    void compute_audioCurrentTime(int);
};
#include "../build/flac_clock_functions.h"
int main(){
    Audio a;
    // Different compressed sizes, highly variable bitrate and split output
    // buffers must all produce exactly 30 seconds for 1,440,000 PCM frames.
    uint32_t n=0;
    while(n<1440000){
        a.m_validSamples=std::min(2048u,1440000-n);n+=a.m_validSamples;
        frameBitrate=n%3?2200000:300000;
        a.compute_audioCurrentTime(n%3?20000:0);
    }
    assert(std::abs(a.m_audioCurrentTime-30.0)<.002);
    a.m_validSamples=0;for(int i=0;i<200;++i)a.compute_audioCurrentTime(12);
    assert(std::abs(a.m_audioCurrentTime-30.0)<.002);
    a.m_audioCurrentTime=100;a.m_validSamples=48000;a.compute_audioCurrentTime(200);
    assert(a.m_audioCurrentTime==101);
    assert(a.m_avr_bitrate==uint64_t(a.m_audioDataSize)*8/249);
    puts("PASS: FLAC clock follows PCM frames, unaffected by VBR bytes, empty output slices or an existing seek offset");
}
