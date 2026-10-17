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
    assert(MAX_BYTES==50u*1024*1024);
    std::puts("PASS: transfer types, UTF-8/space names, encoded traversal/control/FAT rejection, double encoding and byte-length limits");
}
