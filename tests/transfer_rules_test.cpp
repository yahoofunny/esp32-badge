#include <cassert>
#include <cstdio>
#include <string>
#include "../lib/badge_transfer/src/BadgeTransfer.h"
using namespace badge_transfer;
int main(){
    char out[65];
    assert(decodeName("my+lullaby.MP3",out)&&!strcmp(out,"my lullaby.MP3"));
    assert(folderFor("music",out)&&!strcmp(folderFor("music",out),"/Music"));
    assert(decodeName("%E8%8A%B1%E6%9C%B5.jpg",out)&&!strcmp(out,"花朵.jpg"));
    assert(folderFor("photos",out)&&!strcmp(folderFor("photos",out),"/Photos"));
    for(const auto *bad:{"",".","..",".hidden.mp3","../track.mp3","%2e%2e%2ftrack.mp3",
        "%2fMusic%2ftrack.mp3","a%5cb.mp3","a%00.mp3","a%0A.mp3","a%7f.mp3",
        "%252e%252e%252ftrack.mp3","a?b.jpg","a:b.mp3","a%22.jpg","a%","a%0","a%GG.jpg","a.mp3 ","a.mp3."})
        assert(!decodeName(bad,out));
    assert(decodeName((std::string(60,'x')+".mp3").c_str(),out));
    assert(!decodeName((std::string(61,'x')+".mp3").c_str(),out));
    for(const auto *ext:{"mp3","WAV","m4a","AAC","flac","ogg"})assert(folderFor("music",(std::string("a.")+ext).c_str()));
    for(const auto *ext:{"jpg","JPEG","bmp","GIF"})assert(folderFor("photos",(std::string("a.")+ext).c_str()));
    assert(!folderFor("music","a.jpg")&&!folderFor("photos","a.mp3"));
    assert(!folderFor("other","a.mp3")&&!folderFor("photos","a.png"));
    assert(!folderFor("music","a.mp3.exe")&&!folderFor("music","a")&&!folderFor("music","a."));
    assert(maxBytesFor("music")==512u*1024*1024&&maxBytesFor("photos")==50u*1024*1024);
    uint8_t flac[42]={};memcpy(flac,"fLaC",4);flac[4]=0x80;flac[7]=34;flac[10]=0x10;
    flac[18]=0x0a;flac[19]=0xc4;flac[20]=0x42;flac[21]=0xf0;
    auto info=inspectFlac(flac,sizeof flac);
    assert(info.valid&&info.supported&&info.rate==44100&&info.channels==2&&info.bits==16);
    flac[20]|=1;flac[21]=0x70;info=inspectFlac(flac,sizeof flac);
    assert(info.valid&&!info.supported&&info.bits==24);
    flac[20]=0x46;flac[21]=0xf0;assert(!inspectFlac(flac,sizeof flac).supported); // four channels
    flac[20]=0x42;flac[10]=0x40;assert(!inspectFlac(flac,sizeof flac).supported); // oversized block
    assert(!inspectFlac(flac,25).valid);flac[7]=33;assert(!inspectFlac(flac,sizeof flac).valid);
    std::puts("PASS: music/photo size limits, FLAC STREAMINFO compatibility, UTF-8 names, encoded traversal/control/FAT rejection and byte-length limits");
}
