#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

namespace badge_transfer {
constexpr uint32_t MAX_BYTES=50u*1024*1024;
struct Status {
    bool running=false,busy=false;
    uint32_t bytes=0,total=0,completed=0;
    uint64_t freeBytes=0;
    char ssid[24]={0},password[12]={0},message[48]={0};
    int clients=0;
};
// The caller owns the mounted card until end() has joined the HTTP worker.
bool begin();
void end();
void status(Status &out);

// Shared by HTTP validation and host tests. Decode exactly once; never accept a
// path, hidden entry, control byte or FAT-invalid filename from a request.
inline bool decodeName(const char *encoded,char out[65]) {
    size_t n=0;
    for(size_t i=0;encoded[i];++i) {
        unsigned char c=encoded[i];
        if(c=='%') {
            auto hex=[](char x)->int {if(x>='0'&&x<='9')return x-'0';
                if(x>='a'&&x<='f')return x-'a'+10;if(x>='A'&&x<='F')return x-'A'+10;return -1;};
            if(!encoded[i+1]||!encoded[i+2])return false;
            const int a=hex(encoded[i+1]),b=hex(encoded[i+2]);if(a<0||b<0)return false;
            c=(a<<4)|b;i+=2;
        } else if(c=='+')c=' ';
        if(c<32||c==127||strchr("/\\:<>?*|\"%",c)||n>=64)return false;
        out[n++]=char(c);
    }
    out[n]=0;
    return n>0&&out[0]!='.'&&out[n-1]!='.'&&out[n-1]!=' ';
}
inline const char *folderFor(const char *kind,const char *name) {
    const char *dot=strrchr(name,'.');if(!dot||!dot[1])return nullptr;
    char ext[8]={0};size_t n=strlen(dot+1);if(n>=sizeof ext)return nullptr;
    for(size_t i=0;i<n;++i)ext[i]=char(tolower((unsigned char)dot[1+i]));
    const char *music[]={"mp3","wav","m4a","aac","flac","ogg"};
    const char *photos[]={"jpg","jpeg","bmp","gif"};
    if(!strcmp(kind,"music")){for(auto e:music)if(!strcmp(e,ext))return "/Music";}
    if(!strcmp(kind,"photos")){for(auto e:photos)if(!strcmp(e,ext))return "/Photos";}
    return nullptr;
}
}
