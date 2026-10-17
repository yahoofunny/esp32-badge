#include "BadgeTransfer.h"
#include <Arduino.h>
#include <WiFi.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <esp_http_server.h>
#include <atomic>

namespace badge_transfer {
static httpd_handle_t server=nullptr;
static portMUX_TYPE stateLock=portMUX_INITIALIZER_UNLOCKED;
static Status state;
static std::atomic<bool> stopping{false};

static const char WEB[] PROGMEM=R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>吧唧传输</title>
<style>*{box-sizing:border-box}body{margin:0;background:#101019;color:#eddfd1;font:16px/1.6 system-ui,sans-serif}main{max-width:760px;margin:auto;padding:34px 20px 60px}h1{margin:8px 0;font-size:32px}h2{font-size:21px;margin:0 0 8px}p{color:#b3a79e}.tag{color:#f4ac62;font-size:13px;letter-spacing:2px}.card{background:#20202c;border:1px solid #34323d;border-radius:18px;padding:22px;margin-top:20px}button,.pick{display:inline-block;background:#f0a35c;color:#21170f;border:0;border-radius:10px;padding:12px 20px;font:inherit;font-weight:650;cursor:pointer}input[type=file]{display:block;width:100%;margin:16px 0;color:#c8beb7}button:disabled{opacity:.45;cursor:wait}progress{width:100%;accent-color:#f0a35c;height:16px}.muted{color:#a69b93;font-size:14px}#result{white-space:pre-wrap;overflow-wrap:anywhere}.file{display:flex;justify-content:space-between;gap:16px;border-top:1px solid #34323d;padding:9px 0;overflow-wrap:anywhere}.file span:last-child{white-space:nowrap;color:#a69b93}.row{display:flex;justify-content:space-between;align-items:center;gap:16px}#refresh{padding:8px 14px;font-size:14px;background:#34313b;color:#eddfd1}</style>
<main><div class="tag">BADGE · 本地 Wi-Fi</div><h1>吧唧传输</h1><p>把音乐和照片传到吧唧。无需安装 App，文件直接保存在 SD 卡。</p>
<section class="card"><h2>添加音乐</h2><div class="muted">MP3 / WAV / M4A / AAC / FLAC / OGG · 每首最多 50 MB<br>从手机“文件”中选择，选中后检查格式。</div><input id="music" aria-label="选择音乐" type="file" multiple><button data-kind="music">上传音乐</button></section>
<section class="card"><h2>添加照片</h2><div class="muted">JPG / PNG / WebP / GIF / BMP · GIF 保留动画。静态照片转为最长边 1280 像素的 JPG，适合吧唧显示。</div><input id="photos" aria-label="选择照片" type="file" multiple accept="image/jpeg,image/png,image/webp,image/gif,image/bmp,.bmp"><button data-kind="photos">上传照片</button></section>
<section class="card" aria-live="polite"><progress id="progress" value="0" max="100"></progress><div id="result">等待选择文件</div><p class="muted">上传期间请保持热点连接。没有互联网是正常现象；请保持连接，必要时暂停手机的自动网络切换。同名文件会另存，不覆盖原文件。</p></section>
<section class="card"><div class="row"><h2>SD 卡里的文件</h2><button id="refresh">刷新</button></div><div id="files">正在读取…</div></section><p class="muted">完成后，在吧唧上点 STOP 或 BACK 关闭热点，再打开 MUSIC 或 PHOTOS 查看。每个文件夹最多显示 64 个文件。</p></main>
<script>
const $=id=>document.getElementById(id);const size=n=>n>=1048576?(n/1048576).toFixed(1)+' MB':Math.ceil(n/1024)+' KB';let busy=false;
async function refresh(){try{const r=await fetch('/api/files',{cache:'no-store'});if(!r.ok)throw Error('读取失败');const d=await r.json();$('files').replaceChildren();for(const [label,key] of [['音乐','music'],['照片','photos']]){const h=document.createElement('h3');h.textContent=label+' · '+d[key].length;$('files').append(h);for(const f of d[key]){const row=document.createElement('div');row.className='file';for(const text of [f.name,size(f.bytes)]){const s=document.createElement('span');s.textContent=text;row.append(s)}$('files').append(row)}}const p=document.createElement('p');p.className='muted';p.textContent='剩余空间 '+size(d.free);$('files').append(p)}catch(e){$('files').textContent='连接中断，请检查热点连接后重试。'}}
async function photo(file){if(/\.(gif|bmp)$/i.test(file.name))return file;const image=await createImageBitmap(file);const scale=Math.min(1,1280/Math.max(image.width,image.height));const canvas=document.createElement('canvas');canvas.width=Math.max(1,Math.round(image.width*scale));canvas.height=Math.max(1,Math.round(image.height*scale));const c=canvas.getContext('2d');c.fillStyle='#101019';c.fillRect(0,0,canvas.width,canvas.height);c.drawImage(image,0,0,canvas.width,canvas.height);image.close();const blob=await new Promise(resolve=>canvas.toBlob(resolve,'image/jpeg',.92));if(!blob)throw Error('图片转换失败');return new File([blob],file.name.replace(/\.[^.]+$/,'')+'.jpg',{type:'image/jpeg'});}
function upload(kind,file,number,total){return new Promise((resolve,reject)=>{const x=new XMLHttpRequest();x.open('POST','/api/upload?kind='+kind+'&name='+encodeURIComponent(file.name));x.setRequestHeader('Content-Type','application/octet-stream');x.setRequestHeader('X-Badge-Upload','1');x.timeout=300000;x.upload.onprogress=e=>{if(e.lengthComputable){$('progress').value=e.loaded/e.total*100;$('result').textContent=number+' / '+total+' · '+file.name+'\n'+size(e.loaded)+' / '+size(e.total)}};x.onload=()=>{let d;try{d=JSON.parse(x.responseText)}catch{reject(Error('设备返回异常'));return}if(x.status===200)resolve(d);else reject(Error(d.error||'上传失败'))};x.onerror=()=>reject(Error('连接中断，未完成的文件不会加入相册或歌单'));x.ontimeout=()=>reject(Error('上传超时，请重试'));x.send(file)});}
for(const button of document.querySelectorAll('[data-kind]'))button.onclick=async()=>{if(busy)return;const kind=button.dataset.kind,files=[...$(kind).files];if(!files.length){$('result').textContent='请先选择文件';return}busy=true;for(const b of document.querySelectorAll('button'))b.disabled=true;let done=0;try{for(let i=0;i<files.length;i++){let f=files[i];$('result').textContent='正在准备 '+f.name;if(kind==='music'&&!/\.(mp3|wav|m4a|aac|flac|ogg)$/i.test(f.name))throw Error(f.name+' 不是支持的音乐格式，请选择 MP3 / WAV / M4A / AAC / FLAC / OGG');if(kind==='photos')f=await photo(f);if(!f.size||f.size>50*1048576)throw Error(f.name+' 超过 50 MB 或为空');const result=await upload(kind,f,i+1,files.length);done++;$('result').textContent='已保存 '+result.name}$(kind).value='';$('progress').value=100;$('result').textContent='已完成 '+done+' 个文件。关闭传输后即可在吧唧中查看。'}catch(e){$('result').textContent='已完成 '+done+' 个文件。'+e.message}finally{busy=false;for(const b of document.querySelectorAll('button'))b.disabled=false;await refresh()}};
$('refresh').onclick=refresh;refresh();
</script></html>)HTML";

static void message(const char *text,bool busy=false,uint32_t bytes=0,uint32_t total=0) {
    portENTER_CRITICAL(&stateLock);
    snprintf(state.message,sizeof state.message,"%s",text);
    state.busy=busy;state.bytes=bytes;state.total=total;
    portEXIT_CRITICAL(&stateLock);
}
static esp_err_t reply(httpd_req_t *req,const char *body,const char *status="200 OK") {
    httpd_resp_set_status(req,status);httpd_resp_set_type(req,"application/json; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_send(req,body,HTTPD_RESP_USE_STRLEN);
}
static String jsonString(const char *s) {
    String result="\"";
    for(const unsigned char *p=(const unsigned char*)s;*p;++p) {
        if(*p=='"'||*p=='\\')result+='\\';
        if(*p>=32)result+=char(*p);
    }
    return result+'"';
}
static esp_err_t files(httpd_req_t *req) {
    String json="{";
    for(const char *kind:{"music","photos"}) {
        if(json.length()>1)json+=',';
        json+=jsonString(kind)+":[";
        File directory=SD_MMC.open(!strcmp(kind,"music")?"/Music":"/Photos");
        int count=0;
        for(File f=directory.openNextFile();f;f=directory.openNextFile()) {
            if(f.isDirectory()||!folderFor(kind,f.name()))continue;
            if(count++)json+=',';
            json+="{\"name\":"+jsonString(f.name())+",\"bytes\":"+String((unsigned long)f.size())+"}";
            if(count>=64)break;
        }
        json+=']';
    }
    json+=",\"free\":"+String((unsigned long long)(SD_MMC.totalBytes()-SD_MMC.usedBytes()))+"}";
    return reply(req,json.c_str());
}
static esp_err_t upload(httpd_req_t *req) {
    char flag[4]={0},query[320],encoded[240],kind[16],name[65];
    if(httpd_req_get_hdr_value_str(req,"X-Badge-Upload",flag,sizeof flag)!=ESP_OK||strcmp(flag,"1"))
        return reply(req,"{\"error\":\"请使用吧唧传输网页\"}","403 Forbidden");
    if(httpd_req_get_url_query_str(req,query,sizeof query)!=ESP_OK||
       httpd_query_key_value(query,"kind",kind,sizeof kind)!=ESP_OK||
       httpd_query_key_value(query,"name",encoded,sizeof encoded)!=ESP_OK||!decodeName(encoded,name))
        return reply(req,"{\"error\":\"文件名无效或超过 64 字节\"}","400 Bad Request");
    const char *folder=folderFor(kind,name);
    if(!folder)return reply(req,"{\"error\":\"不支持此文件格式\"}","415 Unsupported Media Type");
    if(!req->content_len||req->content_len>MAX_BYTES)
        return reply(req,"{\"error\":\"文件为空或超过 50 MB\"}","413 Content Too Large");
    if(SD_MMC.totalBytes()-SD_MMC.usedBytes()<req->content_len+65536)
        return reply(req,"{\"error\":\"SD 卡空间不足\"}","507 Insufficient Storage");
    int count=0;File directory=SD_MMC.open(folder);
    for(File f=directory.openNextFile();f;f=directory.openNextFile())if(!f.isDirectory()&&folderFor(kind,f.name()))++count;
    directory.close();
    if(count>=64)return reply(req,"{\"error\":\"此文件夹已达到 64 个文件\"}","409 Conflict");
    char target[96],base[65];snprintf(base,sizeof base,"%s",name);
    const char *ext=strrchr(name,'.');base[ext-name]=0;
    snprintf(target,sizeof target,"%s/%s",folder,name);
    unsigned suffix=0;
    while(SD_MMC.exists(target)&&suffix<999)snprintf(target,sizeof target,"%s/%s (%u)%s",folder,base,++suffix,ext);
    if(SD_MMC.exists(target))return reply(req,"{\"error\":\"同名文件过多，请重命名\"}","409 Conflict");
    // One server worker owns this staging file. Startup also removes a leftover
    // from an interrupted power supply; incomplete media never gets indexed.
    const char *temp="/.badge-upload.part";
    File output=SD_MMC.open(temp,FILE_WRITE,true);
    if(!output)return reply(req,"{\"error\":\"无法写入 SD 卡\"}","500 Internal Server Error");
    message("UPLOADING",true,0,req->content_len);
    char buffer[4096];uint32_t received=0;bool ok=true;
    while(received<req->content_len&&!stopping.load()) {
        const int n=httpd_req_recv(req,buffer,min(sizeof buffer,size_t(req->content_len-received)));
        if(n<=0||output.write((const uint8_t*)buffer,n)!=(size_t)n){ok=false;break;}
        received+=n;
        portENTER_CRITICAL(&stateLock);state.bytes=received;portEXIT_CRITICAL(&stateLock);
    }
    output.flush();output.close();
    ok=ok&&!stopping.load()&&received==req->content_len;
    if(ok)ok=SD_MMC.rename(temp,target);
    if(!ok){SD_MMC.remove(temp);message("UPLOAD CANCELLED");return reply(req,"{\"error\":\"上传未完成，请重试\"}","500 Internal Server Error");}
    const auto free=SD_MMC.totalBytes()-SD_MMC.usedBytes();
    portENTER_CRITICAL(&stateLock);++state.completed;state.freeBytes=free;portEXIT_CRITICAL(&stateLock);
    message("SAVED TO SD",false,received,received);
    Serial.printf("[transfer] saved %s bytes=%u\n",target,received);
    String response="{\"name\":"+jsonString(strrchr(target,'/')+1)+",\"bytes\":"+String(received)+"}";
    return reply(req,response.c_str());
}
static esp_err_t root(httpd_req_t *req) {
    httpd_resp_set_type(req,"text/html; charset=utf-8");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    return httpd_resp_send(req,WEB,sizeof WEB-1);
}
bool begin() {
    if(server)return true;
    stopping=false;state=Status{};
    const uint64_t mac=ESP.getEfuseMac();
    snprintf(state.ssid,sizeof state.ssid,"BADGE-%04X",unsigned((mac>>32)&0xffff));
    Preferences prefs;prefs.begin("badge-transfer",false);
    String password=prefs.getString("key","");
    if(password.length()!=8){char generated[12];snprintf(generated,sizeof generated,"%08X",esp_random());password=generated;prefs.putString("key",password);}
    prefs.end();snprintf(state.password,sizeof state.password,"%s",password.c_str());
    WiFi.persistent(false);
    if(!WiFi.mode(WIFI_AP)||!WiFi.softAP(state.ssid,state.password,6,false,2)) {
        WiFi.mode(WIFI_OFF);message("HOTSPOT FAILED");return false;
    }
    SD_MMC.remove("/.badge-upload.part");
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.stack_size=12288;config.core_id=0;
    config.max_open_sockets=3;config.lru_purge_enable=true;config.recv_wait_timeout=2;config.send_wait_timeout=3;
    if(httpd_start(&server,&config)!=ESP_OK){server=nullptr;WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);message("SERVER FAILED");return false;}
    httpd_uri_t home={};home.uri="/";home.method=HTTP_GET;home.handler=root;
    httpd_uri_t list={};list.uri="/api/files";list.method=HTTP_GET;list.handler=files;
    httpd_uri_t put={};put.uri="/api/upload";put.method=HTTP_POST;put.handler=upload;
    if(httpd_register_uri_handler(server,&home)!=ESP_OK||httpd_register_uri_handler(server,&list)!=ESP_OK||httpd_register_uri_handler(server,&put)!=ESP_OK){end();message("SERVER FAILED");return false;}
    state.running=true;state.freeBytes=SD_MMC.totalBytes()-SD_MMC.usedBytes();message("READY TO RECEIVE");
    Serial.printf("[transfer] hotspot=%s url=http://%s\n",state.ssid,WiFi.softAPIP().toString().c_str());
    return true;
}
void end() {
    stopping=true;
    if(server){httpd_stop(server);server=nullptr;}
    WiFi.softAPdisconnect(true);WiFi.mode(WIFI_OFF);
    portENTER_CRITICAL(&stateLock);state.running=false;state.busy=false;portEXIT_CRITICAL(&stateLock);
    message("HOTSPOT OFF");
}
void status(Status &out) {
    portENTER_CRITICAL(&stateLock);out=state;portEXIT_CRITICAL(&stateLock);
    out.clients=out.running?WiFi.softAPgetStationNum():0;
}
}
