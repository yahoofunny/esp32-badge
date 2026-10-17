#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define ESP_IDF_VERSION_MAJOR 5
#define LEFTCHANNEL 0
#define RIGHTCHANNEL 1
#define ESP_OK 0
#define log_e(...) ((void)0)
using esp_err_t=int;
static int writeResult=0,writeCalls=0;
static size_t acceptedBytes=4;
static uint32_t writtenSample=0,pcmSamples=0,nonzeroSamples=0;
static esp_err_t i2s_channel_write(void*,const char *data,size_t,size_t *written,int) {
    ++writeCalls;std::memcpy(&writtenSample,data,4);*written=acceptedBytes;
    return writeResult;
}
class Audio {
public:
    bool playSample(int16_t sample[2]);
    int getBitsPerSample(){return 16;}
    void computeVUlevel(int16_t*){}
    int16_t *IIR_filterChain0(int16_t *p){return p;}
    int16_t *IIR_filterChain1(int16_t *p){return p;}
    int16_t *IIR_filterChain2(int16_t *p){return p;}
    uint32_t Gain(int16_t *p){return uint16_t(p[0])|(uint32_t(uint16_t(p[1]))<<16);}
    float m_corr=1;
    bool m_f_internalDAC=false;
    void *m_i2s_tx_handle=nullptr;
    size_t m_i2s_bytesWritten=0;
    uint32_t m_i2s_samples_written=0,m_i2s_nonzero_written=0,m_i2s_write_errors=0;
};

// Generated from the actual app callback and the vendored playSample method.
// Only unrelated filters, gain and the hardware driver are replaced above.
#include "../build/music_output_functions.h"

int main() {
    uint32_t probe=0x1234abcd;bool forward=false;
    audio_process_i2s(&probe,&forward);
    assert(forward&&probe==0x1234abcd&&pcmSamples==1&&nonzeroSamples==1);
    Audio player;int16_t sample[2]={1234,-4321};
    const uint32_t expected=uint16_t(sample[0])|(uint32_t(uint16_t(sample[1]))<<16);
    assert(player.playSample(sample));
    assert(writeCalls==1&&writtenSample==expected);
    assert(player.m_i2s_samples_written==1&&player.m_i2s_nonzero_written==1);
    int16_t silence[2]={};assert(player.playSample(silence));
    assert(player.m_i2s_samples_written==2&&player.m_i2s_nonzero_written==1);
    // DMA backpressure is a retry, never a successful output frame or error.
    writeResult=263;acceptedBytes=0;
    assert(!player.playSample(sample));
    assert(player.m_i2s_samples_written==2&&player.m_i2s_write_errors==0);
    writeResult=ESP_OK;acceptedBytes=2;assert(!player.playSample(sample));
    assert(player.m_i2s_samples_written==2);
    acceptedBytes=4;assert(player.playSample(sample));
    assert(player.m_i2s_samples_written==3&&player.m_i2s_nonzero_written==2);
    writeResult=-1;acceptedBytes=0;assert(!player.playSample(sample));
    assert(player.m_i2s_samples_written==3&&player.m_i2s_write_errors==1);
    std::puts("PASS: actual music observer forwards unchanged PCM to actual I2S write path; only full accepted frames count, retries/errors do not");
}
