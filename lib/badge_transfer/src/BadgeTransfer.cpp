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
<style>[hidden]{display:none!important}.feedback{white-space:pre-wrap;overflow-wrap:anywhere;margin:14px 0 8px;font-size:14px}button[data-cancel]{background:#383340;color:#eddfd1}</style>
<main><div class="tag">BADGE · 本地 Wi-Fi</div><h1>吧唧传输</h1><p>把音乐和照片传到吧唧。无需安装 App，文件直接保存在 SD 卡。</p>
<section class="card"><h2>添加音乐</h2><div class="muted">MP3 / WAV / M4A / AAC / FLAC / OGG · 每首最多 512 MB<br>从手机“文件”中选择，选中后检查格式。16 位 FLAC 可直接试听；24 位 FLAC 会提示兼容性。</div><input id="music" aria-label="选择音乐" type="file" multiple><button data-kind="music">上传音乐</button> <button data-cancel="music" hidden>取消上传</button><div class="feedback" id="music-feedback" role="status" aria-live="polite"></div><progress id="music-progress" value="0" max="100" hidden></progress></section>
<section class="card"><h2>添加照片</h2><div class="muted">JPG / PNG / WebP / GIF / BMP · 每张最多 50 MB。GIF 保留动画。静态照片转为最长边 1280 像素的 JPG，适合吧唧显示。</div><input id="photos" aria-label="选择照片" type="file" multiple accept="image/jpeg,image/png,image/webp,image/gif,image/bmp,.bmp"><button data-kind="photos">上传照片</button> <button data-cancel="photos" hidden>取消上传</button><div class="feedback" id="photos-feedback" role="status" aria-live="polite"></div><progress id="photos-progress" value="0" max="100" hidden></progress></section>
<section class="card" aria-live="polite"><progress id="progress" value="0" max="100"></progress><div id="result">等待选择文件</div><p class="muted">上传期间请保持热点连接。没有互联网是正常现象；请保持连接，必要时暂停手机的自动网络切换。同名文件会另存，不覆盖原文件。</p></section>
<section class="card"><div class="row"><h2>SD 卡里的文件</h2><button id="refresh">刷新</button></div><div id="files">正在读取…</div></section><p class="muted">完成后，在吧唧上点 STOP 或 BACK 关闭热点，再打开 MUSIC 或 PHOTOS 查看。每个文件夹最多显示 64 个文件。</p></main>
<script>
const $=id=>document.getElementById(id);const size=n=>n>=1048576?(n/1048576).toFixed(1)+' MB':Math.ceil(n/1024)+' KB';let busy=false,currentRequest=null,cancelRequested=false;
function show(kind,text,error=false,reveal=false){const box=$(kind+'-feedback');box.textContent=text;box.style.color=error?'#ffb1a2':'#eddfd1';$('result').textContent=text;if(reveal)box.scrollIntoView({block:'nearest'});}
function checkFile(kind,file){if(kind==='music'&&!/\.(mp3|wav|m4a|aac|flac|ogg)$/i.test(file.name))throw Error(file.name+' 不是支持的音乐格式');if(!file.size)throw Error(file.name+' 是空文件');const limit=kind==='music'?512:50;if(file.size>limit*1048576)throw Error(file.name+' 超过 '+limit+' MB');if(new TextEncoder().encode(file.name).length>64)throw Error('文件名过长，请缩短名称后重试（最多 64 字节）');}
async function refresh(){try{const r=await fetch('/api/files',{cache:'no-store'});if(!r.ok)throw Error('读取失败');const d=await r.json();$('files').replaceChildren();for(const [label,key] of [['音乐','music'],['照片','photos']]){const h=document.createElement('h3');h.textContent=label+' · '+d[key].length;$('files').append(h);for(const f of d[key]){const row=document.createElement('div');row.className='file';for(const text of [f.name,size(f.bytes)]){const s=document.createElement('span');s.textContent=text;row.append(s)}$('files').append(row)}}const p=document.createElement('p');p.className='muted';p.textContent='剩余空间 '+size(d.free);$('files').append(p)}catch(e){$('files').textContent='连接中断，请检查热点连接后重试。'}}
async function photo(file){if(/\.(gif|bmp)$/i.test(file.name))return file;const image=await createImageBitmap(file);const scale=Math.min(1,1280/Math.max(image.width,image.height));const canvas=document.createElement('canvas');canvas.width=Math.max(1,Math.round(image.width*scale));canvas.height=Math.max(1,Math.round(image.height*scale));const c=canvas.getContext('2d');c.fillStyle='#101019';c.fillRect(0,0,canvas.width,canvas.height);c.drawImage(image,0,0,canvas.width,canvas.height);image.close();const blob=await new Promise(resolve=>canvas.toBlob(resolve,'image/jpeg',.92));if(!blob)throw Error('图片转换失败');return new File([blob],file.name.replace(/\.[^.]+$/,'')+'.jpg',{type:'image/jpeg'});}
function upload(kind,file,number,total){return new Promise((resolve,reject)=>{
 const x=new XMLHttpRequest();currentRequest=x;let lastActivity=Date.now(),stalled=false,settled=false;
 // No total duration limit: a large file may take several minutes on local Wi-Fi.
 x.open('POST','/api/upload?kind='+kind+'&name='+encodeURIComponent(file.name));x.timeout=0;
 x.setRequestHeader('Content-Type','application/octet-stream');x.setRequestHeader('X-Badge-Upload','1');
 const timer=setInterval(()=>{if(Date.now()-lastActivity>45000){stalled=true;x.abort()}},1000);
 function finish(error,result){if(settled)return;settled=true;clearInterval(timer);if(currentRequest===x)currentRequest=null;error?reject(error):resolve(result);}
 show(kind,number+' / '+total+' · '+file.name+'\n准备发送 '+size(file.size)+'，等待手机读取文件…');
 x.upload.onprogress=e=>{lastActivity=Date.now();if(e.lengthComputable){const p=e.loaded/e.total*100;$('progress').value=$(kind+'-progress').value=p;show(kind,number+' / '+total+' · '+file.name+'\n'+size(e.loaded)+' / '+size(e.total)+' · '+p.toFixed(1)+'%'+(e.loaded===e.total?'\n发送完毕，等待设备确认写入…':''))}};
 x.onload=()=>{let d;try{d=JSON.parse(x.responseText)}catch{finish(Error('设备返回异常，请刷新后重试'));return}finish(x.status===200?null:Error(d.error||'上传失败'),d)};
 x.onerror=()=>finish(Error('连接中断，请检查热点。未完成的数据会清理，可安全重试。'));
 x.onabort=()=>finish(Error(stalled?'45 秒没有传输进度，已停止等待，可安全重试。':'已取消上传。已完成的文件保留，未完成的数据会清理。'));
 try{x.send(file)}catch(e){finish(e)}
});}
for(const kind of ['music','photos'])$(kind).onchange=()=>{const files=[...$(kind).files];show(kind,files.length?'已选择 '+files.length+' 个文件 · '+size(files.reduce((n,f)=>n+f.size,0)):'未选择文件');};
for(const button of document.querySelectorAll('[data-cancel]'))button.onclick=()=>{cancelRequested=true;button.disabled=true;show(button.dataset.cancel,'正在取消…');if(currentRequest)currentRequest.abort();};
for(const button of document.querySelectorAll('[data-kind]'))button.onclick=async()=>{
 if(busy)return;const kind=button.dataset.kind,files=[...$(kind).files];
 if(!files.length){show(kind,'请先选择文件',true,true);return}
 busy=true;cancelRequested=false;for(const b of document.querySelectorAll('button,input[type=file]'))b.disabled=true;
 const cancel=document.querySelector('[data-cancel="'+kind+'"]');cancel.hidden=false;cancel.disabled=false;
 const bar=$(kind+'-progress');bar.hidden=false;bar.value=0;$('progress').value=0;
 let done=0;const warnings=[];
 try{show(kind,'正在准备文件…',false,true);
  for(let i=0;i<files.length;i++){
   if(cancelRequested)throw Error('已取消上传，已完成的文件保留。');let f=files[i];show(kind,'正在准备 '+f.name);
   if(kind==='photos')f=await photo(f);checkFile(kind,f);
   if(cancelRequested)throw Error('已取消上传，已完成的文件保留。');
   const result=await upload(kind,f,i+1,files.length);done++;
   if(result.audio&&!result.audio.supported)warnings.push(result.name+' 已保存，但当前播放器不支持此 FLAC（'+result.audio.bits+' 位 / '+result.audio.channels+' 声道）。可改传 16 位双声道 FLAC 或 MP3。');
   show(kind,'已保存 '+result.name);
  }
  $(kind).value='';$('progress').value=bar.value=100;
  show(kind,'已完成 '+done+' 个文件。关闭传输后即可在吧唧中查看。'+(warnings.length?'\n'+warnings.join('\n'):''),warnings.length>0,true);
 }catch(e){show(kind,'已完成 '+done+' 个文件。'+e.message,true,true)}
 finally{busy=false;currentRequest=null;cancel.hidden=true;for(const b of document.querySelectorAll('button,input[type=file]'))b.disabled=false;await refresh()}
};
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
    if(!req->content_len||req->content_len>maxBytesFor(kind))
        return reply(req,!strcmp(kind,"music")?"{\"error\":\"音乐为空或超过 512 MB\"}":"{\"error\":\"照片为空或超过 50 MB\"}","413 Content Too Large");
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
    char buffer[4096];uint8_t header[42]={0};size_t headerBytes=0;
    uint32_t received=0,lastDataMs=millis();bool ok=true;
    while(received<req->content_len&&!stopping.load()) {
        const int n=httpd_req_recv(req,buffer,min(sizeof buffer,size_t(req->content_len-received)));
        if(n==HTTPD_SOCK_ERR_TIMEOUT && uint32_t(millis()-lastDataMs)<45000)continue;
        if(n<=0||output.write((const uint8_t*)buffer,n)!=(size_t)n){ok=false;break;}
        lastDataMs=millis();
        const size_t capture=min(size_t(n),sizeof header-headerBytes);
        if(capture){memcpy(header+headerBytes,buffer,capture);headerBytes+=capture;}
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
    const FlacInfo audio=inspectFlac(header,headerBytes);
    if(audio.valid) {
        Serial.printf("[transfer] FLAC rate=%u bits=%u channels=%u supported=%d\n",audio.rate,audio.bits,audio.channels,audio.supported);
        response.remove(response.length()-1);
        response+=",\"audio\":{\"rate\":"+String(audio.rate)+",\"bits\":"+String(unsigned(audio.bits))+",\"channels\":"+String(unsigned(audio.channels))+",\"supported\":"+(audio.supported?"true":"false")+"}}";
    }
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
    config.max_open_sockets=3;config.lru_purge_enable=true;config.recv_wait_timeout=1;config.send_wait_timeout=3;
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
