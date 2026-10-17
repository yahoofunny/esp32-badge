#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../lib/badge_audio_i2s/src/flac_decoder/flac_decoder.h"
extern int32_t **s_samplesBuffer;
extern uint16_t s_blockSize,s_outputOffset;
extern uint8_t s_flacStatus;
extern std::vector<int32_t> coefs;

int main(int argc,char **argv){
    assert(argc==3 && FLACDecoder_AllocateBuffers());
    // A negative LPC shift and 24-bit constant would overflow/truncate before
    // decoding music. Exercise these independently of encoder heuristics.
    coefs={32767,32767};s_blockSize=3;
    s_samplesBuffer[0][0]=100000;s_samplesBuffer[0][1]=-90000;s_samplesBuffer[0][2]=123;
    restoreLinearPrediction(0,4);
    assert(s_samplesBuffer[0][2]==int32_t((int64_t(10000)*32767)>>4)+123);
    coefs={1};s_blockSize=2;s_samplesBuffer[0][0]=12345;s_samplesBuffer[0][1]=5;
    restoreLinearPrediction(0,-2);assert(s_samplesBuffer[0][1]==49385);
    FLACDecoder_setDefaults();
    std::ifstream input(argv[1],std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(input)),{});
    assert(data.size()>42 && !memcmp(data.data(),"fLaC",4));
    auto *h=data.data();
    uint32_t rate=(uint32_t(h[18])<<12)|(uint32_t(h[19])<<4)|(h[20]>>4);
    unsigned ch=((h[20]>>1)&7)+1,bits=((h[20]&1)<<4)+(h[21]>>4)+1;
    uint64_t total=h[21]&15;for(int i=22;i<26;++i)total=(total<<8)|h[i];
    size_t pos=4;bool last=false;
    while(!last){last=data[pos]&128;size_t n=(size_t(data[pos+1])<<16)|(data[pos+2]<<8)|data[pos+3];pos+=4+n;assert(pos<=data.size());}
    data.resize(data.size()+65536); // Ring-buffer lookahead, as on the device.
    FLACSetRawBlockParams(ch,rate,bits,uint32_t(total),0);
    std::ofstream output(argv[2],std::ios::binary);uint64_t produced=0;
    int16_t pcm[4096];
    while(produced<total*ch){
        int left=int(data.size()-pos),before=left;
        int status=FLACDecodeNative(data.data()+pos,&left,pcm);
        if(status<0){fprintf(stderr,"decode error %d at %zu\n",status,pos);return 2;}
        assert(left>=0 && left<=before);pos+=before-left;
        unsigned n=FLACGetOutputSamps();assert(produced+n<=total*ch);
        output.write(reinterpret_cast<char*>(pcm),n*2);produced+=n;
        assert(n || before!=left);
    }
    assert(FLACGetBitsPerSample()==16 && FLACGetAudioFileDuration()==total/rate);
    // Abandoning a partially emitted frame must not carry its offset to a song.
    s_outputOffset=2048;FLACDecoderReset();assert(s_outputOffset==0);
    FLACDecoder_FreeBuffers();
    printf("PASS %u-bit %u Hz %u channels: %llu PCM samples\n",bits,rate,ch,(unsigned long long)produced);
}
