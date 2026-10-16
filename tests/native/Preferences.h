#pragma once
#include <map>
#include <string>
#include <vector>
#include <cstring>
class Preferences {
    std::string prefix;
public:
    inline static std::map<std::string,std::vector<unsigned char>> blobs;
    bool begin(const char *name, bool readOnly=false) {
        prefix=std::string(name)+"/";
        return !readOnly || blobs.count(prefix+"blob");
    }
    void end(){}
    bool isKey(const char *key) {return blobs.count(prefix+key);}
    size_t getBytes(const char *key,void* out,size_t cap) {
        auto it=blobs.find(prefix+key);
        if(it==blobs.end() || it->second.size()>cap)return 0;
        memcpy(out,it->second.data(),it->second.size());return it->second.size();
    }
    size_t putBytes(const char *key,const void* data,size_t n) {
        const auto *p=static_cast<const unsigned char*>(data);
        blobs[prefix+key]={p,p+n};return n;
    }
};
