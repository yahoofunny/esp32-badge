#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../lib/badge_transfer/src/BadgeTransfer.h"
using std::min;
using namespace badge_transfer;
using esp_err_t=int;
constexpr int ESP_OK=0,HTTPD_RESP_USE_STRLEN=-1,FILE_WRITE=1;
// Hardware/network stubs; the production request handler itself is extracted below.
class String {
    std::string s;
public:
    String(const char *v=""):s(v){}
    String(char c):s(1,c){}
    String(unsigned long long v):s(std::to_string(v)){}
    String(uint32_t v):s(std::to_string(v)){}
    const char *c_str()const{return s.c_str();}
    String &operator+=(char c){s+=c;return *this;}
    String &operator+=(const String &v){s+=v.s;return *this;}
    friend String operator+(String a,const String &b){return a+=b;}
};
struct Entry {std::string bytes;bool directory=false;};
static std::map<std::string,std::shared_ptr<Entry>> disk;
static bool writeFailure=false,renameFailure=false;
class File {
    std::shared_ptr<Entry> entry;
    std::string path;
    std::vector<std::string> children;
    size_t next=0;
public:
    File()=default;
    File(std::string p,std::shared_ptr<Entry> e):entry(e),path(p){
        if(e&&e->directory)for(auto &item:disk)if(item.first.rfind(p+"/",0)==0)children.push_back(item.first);
    }
    operator bool()const{return bool(entry);}
    bool isDirectory()const{return entry&&entry->directory;}
    const char *name()const{return strrchr(path.c_str(),'/')+1;}
    File openNextFile(){if(next>=children.size())return File();const auto p=children[next++];return File(p,disk.at(p));}
    size_t write(const uint8_t *b,size_t n){if(writeFailure)return 0;entry->bytes.append((const char*)b,n);return n;}
    void flush(){}
    void close(){entry.reset();}
};
static struct {
    uint64_t free=1000000000;
    uint64_t totalBytes(){return 2000000000;}
    uint64_t usedBytes(){return totalBytes()-free;}
    bool exists(const char *p){return disk.count(p);}
    File open(const char *p,int mode=0,bool=false){
        if(mode==FILE_WRITE)disk[p]=std::make_shared<Entry>();
        return disk.count(p)?File(p,disk.at(p)):File();
    }
    bool remove(const char *p){return disk.erase(p);}
    bool rename(const char *a,const char *b){if(renameFailure||!disk.count(a)||disk.count(b))return false;disk[b]=disk.at(a);disk.erase(a);return true;}
} SD_MMC;
struct httpd_req_t {
    std::string query,body,response,status="200 OK";
    size_t content_len=0,cursor=0,disconnectAt=SIZE_MAX;
    bool flag=true;
};
static int httpd_req_get_hdr_value_str(httpd_req_t *r,const char*,char *s,size_t n){snprintf(s,n,"%s",r->flag?"1":"");return r->flag?0:1;}
static int httpd_req_get_url_query_str(httpd_req_t *r,char *s,size_t n){if(r->query.size()>=n)return 1;strcpy(s,r->query.c_str());return 0;}
static int httpd_query_key_value(const char *query,const char *key,char *s,size_t n){
    const std::string q=query,prefix=std::string(key)+"=";size_t at=q.find(prefix);
    if(at==std::string::npos)return 1;at+=prefix.size();const auto v=q.substr(at,q.find('&',at)-at);
    if(v.size()>=n)return 1;strcpy(s,v.c_str());return 0;
}
static int httpd_req_recv(httpd_req_t *r,char *out,size_t n){
    if(r->cursor>=r->disconnectAt||r->cursor>=r->body.size())return -1;
    n=min({n,size_t(1024),r->body.size()-r->cursor,r->disconnectAt-r->cursor});
    memcpy(out,r->body.data()+r->cursor,n);r->cursor+=n;return int(n);
}
static void httpd_resp_set_status(httpd_req_t *r,const char *v){r->status=v;}
static void httpd_resp_set_type(httpd_req_t*,const char*){}
static void httpd_resp_set_hdr(httpd_req_t*,const char*,const char*){}
static esp_err_t httpd_resp_send(httpd_req_t *r,const char *data,int){r->response=data;return ESP_OK;}
static Status state;
static int stateLock;
static std::atomic<bool> stopping{false};
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static struct {template<typename... T>void printf(const char*,T...){}} Serial;
#include "../build/transfer_http_functions.h"

static httpd_req_t request(std::string name="song.mp3"){
    httpd_req_t r;r.query="kind=music&name="+name;r.body=std::string(10000,'x');r.content_len=r.body.size();return r;
}
int main(){
    disk["/Music"]=std::make_shared<Entry>();disk["/Music"]->directory=true;
    disk["/Photos"]=std::make_shared<Entry>();disk["/Photos"]->directory=true;
    auto first=request();upload(&first);
    assert(first.status=="200 OK"&&disk.at("/Music/song.mp3")->bytes==first.body);
    assert(state.completed==1&&!state.busy&&state.bytes==first.body.size());
    auto second=request();second.body[0]='y';upload(&second);
    assert(second.status=="200 OK"&&disk.at("/Music/song (1).mp3")->bytes==second.body);
    assert(disk.at("/Music/song.mp3")->bytes==first.body);
    auto abort=request("aborted.mp3");abort.disconnectAt=2048;upload(&abort);
    assert(abort.status=="500 Internal Server Error"&&!disk.count("/Music/aborted.mp3")&&!disk.count("/.badge-upload.part"));
    auto bad=request("%2e%2e%2fwrong.mp3");upload(&bad);assert(bad.status=="400 Bad Request");
    bad=request("image.png");upload(&bad);assert(bad.status=="415 Unsupported Media Type");
    bad=request();bad.flag=false;upload(&bad);assert(bad.status=="403 Forbidden");
    bad=request();bad.content_len=MAX_BYTES+1;upload(&bad);assert(bad.status=="413 Content Too Large");
    bad=request();bad.content_len=0;upload(&bad);assert(bad.status=="413 Content Too Large");
    SD_MMC.free=1;bad=request();upload(&bad);assert(bad.status=="507 Insufficient Storage");SD_MMC.free=1000000000;
    writeFailure=true;bad=request("write-error.mp3");upload(&bad);writeFailure=false;
    assert(bad.status=="500 Internal Server Error"&&!disk.count("/.badge-upload.part")&&!disk.count("/Music/write-error.mp3"));
    renameFailure=true;bad=request("rename-error.mp3");upload(&bad);renameFailure=false;
    assert(bad.status=="500 Internal Server Error"&&!disk.count("/.badge-upload.part")&&!disk.count("/Music/rename-error.mp3"));
    stopping=true;bad=request("stopping.mp3");upload(&bad);stopping=false;
    assert(bad.status=="500 Internal Server Error"&&!disk.count("/.badge-upload.part"));
    for(int i=2;i<64;++i)disk["/Music/filler"+std::to_string(i)+".mp3"]=std::make_shared<Entry>();
    bad=request("full.mp3");upload(&bad);assert(bad.status=="409 Conflict"&&!disk.count("/Music/full.mp3"));
    assert(state.completed==2&&disk.at("/Music/song.mp3")->bytes==first.body);
    std::puts("PASS: real HTTP upload handler, chunked writes, collision preservation, abort/stop/write/rename cleanup, type/name/size/free-space/card limits");
}
