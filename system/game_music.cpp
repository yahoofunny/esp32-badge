#include "game_music.h"
#include <Arduino.h>
#include <SD_MMC.h>
#include <BadgeAudio.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>
#include "audio.h"
#include "input.h"
#include "media.h"
#include "sound.h"

static const uint16_t COL_BG=PX(RGB565(0x0B,0x0B,0x16));
static const uint16_t COL_TEXT=PX(RGB565(0xEB,0xDF,0xCF));
static const uint16_t COL_DIM=PX(RGB565(0x86,0x72,0x66));
static const uint16_t COL_ACCENT=PX(RGB565(0xF0,0x9A,0x3C));
static const uint16_t COL_WARN  = PX(RGB565(0xE0, 0x50, 0x50));
static const uint16_t COL_TRACK=PX(RGB565(0x28,0x22,0x2B));
static const char *const MUSIC_EXTS[]={"mp3","wav","m4a","aac","flac","ogg"};
static bool songFinished=false;
static uint32_t pcmSamples=0,nonzeroSamples=0;
// The decoder must keep feeding DMA while the UI draws or waits for the LCD.
// All player/SD access shares this recursive lock, including track changes.
struct MusicPump {
    SemaphoreHandle_t lock=nullptr,stopped=nullptr;
    TaskHandle_t task=nullptr;
    uint32_t lastCallUs=0,maxCallUs=0,maxGapUs=0;
};
struct MusicGuard {
    MusicPump *pump;
    explicit MusicGuard(MusicPump *p):pump(p) {
        if(pump && pump->lock)xSemaphoreTakeRecursive(pump->lock,portMAX_DELAY);
    }
    ~MusicGuard(){if(pump && pump->lock)xSemaphoreGiveRecursive(pump->lock);}
};
void audio_eof_mp3(const char*){songFinished=true;}
void audio_info(const char *message){Serial.printf("[music-audio] %.180s\n",message);}
void audio_process_i2s(uint32_t *sample,bool *continueI2S) {
    ++pcmSamples;if(*sample)++nonzeroSamples;
    // The library defaults this flag to false: observing must not consume audio.
    *continueI2S=true;
}

// An original eight-second chiptune gives an empty card something to play.
// Create only when /Music contains no supported tracks; never replace user music.
static bool createDemo() {
    constexpr uint32_t rate=16000,samples=rate*8,dataBytes=samples*2;
    const char *path="/Music/Badge-Loop.wav";
    if(SD_MMC.exists(path))return false;
    File f=SD_MMC.open(path,FILE_WRITE);if(!f)return false;
    uint8_t header[44]={0};
    memcpy(header,"RIFF",4);memcpy(header+8,"WAVEfmt ",8);memcpy(header+36,"data",4);
    auto le32=[&](int at,uint32_t value){for(int i=0;i<4;++i)header[at+i]=uint8_t(value>>(i*8));};
    le32(4,36+dataBytes);le32(16,16);header[20]=1;header[22]=1;
    le32(24,rate);le32(28,rate*2);header[32]=2;header[34]=16;le32(40,dataBytes);
    bool ok=f.write(header,44)==44;
    const int notes[]={523,659,784,659,587,698,880,698,659,784,988,784,587,659,784,523};
    uint32_t phase=0;int16_t pcm[256];
    for(uint32_t at=0;ok && at<samples;at+=256) {
        const uint32_t n=(samples-at<256)?samples-at:256;
        for(uint32_t j=0;j<n;++j) {
            const uint32_t position=at+j,note=position/(rate/2),within=position%(rate/2);
            phase+=uint32_t(uint64_t(notes[note%16])*4294967296ULL/rate);
            const int v=phase>>16;
            const int triangle=v<32768?v*2-32768:98303-v*2;
            const float envelope=fminf(1.0f,within/160.0f)*(1.0f-within/float(rate/2));
            pcm[j]=int16_t(triangle*.16f*envelope);
        }
        ok=f.write(reinterpret_cast<uint8_t*>(pcm),n*2)==n*2;
        if((at&8191)==0)delay(1);
    }
    f.close();
    if(!ok)SD_MMC.remove(path);
    Serial.printf("[music] original demo created=%d bytes=%u\n",ok,44+dataBytes);
    return ok;
}

// The bin, right of the record. Two taps: the first arms it, the second removes the
// track that is loaded. Kept away from the transport buttons for that reason.
static const int BIN_X = 252, BIN_Y = 126, BIN_R = 15;
static bool deleteArmed = false;

static int musicTarget(int x,int y) {
    const int bx=x-BIN_X,by=y-BIN_Y;
    if(bx*bx+by*by<=BIN_R*BIN_R)return 6;
    if(y>=231 && y<=255 && x>=58 && x<=302)return 5;
    const int xs[]={110,180,250,86,274},ys[]={290,290,290,75,75};
    for(int i=0;i<5;++i) {
        const int dx=x-xs[i],dy=y-ys[i],r=i<3?28:20;
        if(dx*dx+dy*dy<=r*r)return i;
    }
    return -1;
}

void GameMusic::enter() {
    index=0;tracks=0;paused=failed=tracking=moved=painted=false;
    redraw=true;pressed=-1;elapsed=duration=lastFrame=0;rotation=0;
    pcmSamples=nonzeroSamples=0;songFinished=false;
    if(!sound::suspend()){failed=true;return;}
    volume=audio::volume();if(volume<0)volume=60;
    if(media::begin()) {
        tracks=media::list(media::MUSIC_DIR,MUSIC_EXTS,6);
        if(!tracks && createDemo())tracks=media::list(media::MUSIC_DIR,MUSIC_EXTS,6);
    }
    if(!tracks)return;
    pump=new MusicPump;
    pump->lock=xSemaphoreCreateRecursiveMutex();
    pump->stopped=xSemaphoreCreateBinary();
    if(!pump->lock || !pump->stopped){failed=true;return;}
    player=new Audio(false,3,I2S_NUM_0);
    player->setBufsize(8192,256*1024);
    if(!player->setPinout(48,38,47,2)){failed=true;return;}
    player->forceMono(true);player->setVolume(21);
    playCurrent();
    if(xTaskCreatePinnedToCore(pumpAudio,"badge-music",8192,this,2,&pump->task,0)!=pdPASS) {
        failed=true;player->stopSong();digitalWrite(9,LOW);
        Serial.println("[music] audio task could not start");
    }
}
void GameMusic::pumpAudio(void *context) {
    auto &music=*static_cast<GameMusic*>(context);
    for(;;) {
        if(ulTaskNotifyTake(pdTRUE,0))break;
        {
            MusicGuard guard(music.pump);
            const uint32_t started=micros();
            auto &p=*music.pump;
            if(p.lastCallUs)p.maxGapUs=max(p.maxGapUs,uint32_t(started-p.lastCallUs));
            if(music.player)music.player->loop();
            p.lastCallUs=micros();
            p.maxCallUs=max(p.maxCallUs,uint32_t(p.lastCallUs-started));
        }
        // DMA writes are nonblocking. Yield on a full buffer so core 0's idle
        // task and USB/system work run as well; never spin at audio priority.
        vTaskDelay(1);
    }
    // Acknowledge before leave() deletes the decoder or unmounts the card.
    xSemaphoreGive(music.pump->stopped);
    vTaskDelete(nullptr);
}
void GameMusic::leave() {
    digitalWrite(9,LOW);
    if(pump && pump->task) {
        xTaskNotifyGive(pump->task);
        xSemaphoreTake(pump->stopped,portMAX_DELAY);
    }
    if(player){player->stopSong();delete player;player=nullptr;}
    if(pump) {
        if(pump->lock)vSemaphoreDelete(pump->lock);
        if(pump->stopped)vSemaphoreDelete(pump->stopped);
        delete pump;pump=nullptr;
    }
    media::end();
    const bool ok=sound::begin();
    Serial.printf("[music] returned audio to launcher=%d\n",ok);
}
void GameMusic::playCurrent() {
    MusicGuard guard(pump);
    if(!player || tracks<=0)return;
    songFinished=false;elapsed=duration=0;paused=false;
    failed=!player->connecttoFS(SD_MMC,media::pathAt(index));
    digitalWrite(9,failed?LOW:HIGH);
    snprintf(title,sizeof title,"%.24s",media::nameAt(index));
    for(const unsigned char *p=reinterpret_cast<const unsigned char*>(title);*p;++p)
        if(*p>=128){snprintf(title,sizeof title,"TRACK %02d",index+1);break;}
    char *dot=strrchr(title,'.');if(dot)*dot=0;
    redraw=true;
    Serial.printf("[music] %d/%d %s: %s\n",index+1,tracks,media::nameAt(index),failed?"FAILED":"playing");
}
// The continuous volume control, between the two round buttons that step by ten.
static const int VOL_X0 = 112, VOL_X1 = 248, VOL_Y = 75;

void GameMusic::nextTrack(int direction) {
    MusicGuard guard(pump);
    if(!tracks)return;
    index=(index+(direction<0?tracks-1:1))%tracks;playCurrent();
}
void GameMusic::togglePlayback() {
    MusicGuard guard(pump);
    if(!player || !tracks)return;
    if(failed || (!paused && !player->isRunning()))playCurrent();
    else if(player->pauseResume())paused=!paused;
    redraw=true;
}
bool GameMusic::setVolumePercent(int percent,bool remember) {
    const int next=constrain(percent,0,100);
    if(!audio::setVolume(next))return false;
    volume=next;
    if(remember)audio::storeVolume(volume);
    redraw=true;return true;
}
void GameMusic::printDiagnostics() const {
    MusicGuard guard(pump);
    Serial.printf("[music] track=%d/%d running=%d paused=%d error=%d time=%u/%u rate=%u pcm=%u nonzero=%u i2s_written=%u i2s_nonzero=%u i2s_errors=%u volume=%d amp=%d uptime_ms=%u pump_max_us=%u pump_gap_us=%u stack_free=%u\n",
        index+1,tracks,player&&player->isRunning(),paused,failed,elapsed,duration,
        player?player->getSampleRate():0,pcmSamples,nonzeroSamples,
        player?player->getI2SSamplesWritten():0,player?player->getI2SNonzeroSamplesWritten():0,
        player?player->getI2SWriteErrors():0,audio::volume(),digitalRead(9),millis(),
        pump?pump->maxCallUs:0,pump?pump->maxGapUs:0,
        pump&&pump->task?uxTaskGetStackHighWaterMark(pump->task):0);
}
void GameMusic::update(float dt) {
    if(player) {
        MusicGuard guard(pump);
        elapsed=player->getAudioCurrentTime();duration=player->getAudioFileDuration();
        if(songFinished){songFinished=false;nextTrack();}
        else if(!paused && !failed && !player->isRunning()) {
            failed=true;redraw=true;digitalWrite(9,LOW);
            Serial.println("[music] decoder stopped before EOF");
        }
    }
    if(!paused && !failed && tracks)rotation=fmodf(rotation+dt*1.8f,2*PI_F);
    const int oldPressed=pressed;
    {
        static bool dragging = false;
        const int x=input::tapX(),y=input::tapY();
        const bool onBar=(x>=VOL_X0 && x<=VOL_X1 && y>=VOL_Y-16 && y<=VOL_Y+18);
        if(dragging || (input::touchDown() && onBar)) {
            if(input::touchDown()) {
                dragging=true;
                const int v=audio::percentForSlider(constrain((x-VOL_X0)*100/(VOL_X1-VOL_X0),0,100));
                if(v!=volume){setVolumePercent(v,false);redraw=true;}
                pressed=-1;
            } else {
                dragging=false;
                setVolumePercent(volume,true);
            }
        } else if(input::touchDown()) {
            const int x2=x,y2=y;
            if(!tracking){tracking=true;moved=false;startX=x2;startY=y2;startTarget=musicTarget(x2,y2);}
            const int dx=x2-startX,dy=y2-startY;if(dx*dx+dy*dy>14*14)moved=true;
            pressed=(!moved && musicTarget(x2,y2)==startTarget)?startTarget:-1;
        } else {
        if(tracking && !moved && input::tapped() && musicTarget(input::tapX(),input::tapY())==startTarget) {
            if(startTarget!=6 && deleteArmed){deleteArmed=false;redraw=true;}
            if(startTarget==0)nextTrack(-1);
            else if(startTarget==1)togglePlayback();
            else if(startTarget==2)nextTrack();
            else if(startTarget==3 || startTarget==4) {
                setVolumePercent(volume+(startTarget==3?-10:10));
            } else if(startTarget==6) {
                if(!deleteArmed) { deleteArmed=true; redraw=true; }
                else deleteArmed=false, deleteCurrentTrack();
            } else if(startTarget==5 && player && duration>0) {
                MusicGuard guard(pump);
                const int x=constrain(input::tapX(),66,294);
                player->setAudioPlayPosition(uint16_t((x-66)*duration/228));redraw=true;
            }
        }
        tracking=false;pressed=-1;
        }
    }
    if(oldPressed!=pressed)redraw=true;
}

void GameMusic::deleteCurrentTrack()
{
    if (!tracks) return;
    const char *path = media::pathAt(index);
    if (!*path) return;
    if (SD_MMC.remove(path)) {
        Serial.printf("[music] removed %s", path); Serial.println();
        tracks = media::list(media::MUSIC_DIR, MUSIC_EXTS, 6);
        if (tracks <= 0) { index = 0; return; }
        if (index >= tracks) index = tracks - 1;
        playCurrent();
    } else {
        Serial.printf("[music] could not remove %s", path); Serial.println();
    }
}

void GameMusic::render() {
    const uint32_t now=millis();
    if(painted && !redraw && uint32_t(now-lastFrame)<80){setDirty(1,0);return;}
    lastFrame=now;painted=true;redraw=false;setDirty(0,359);
    gfx::clear(COL_BG);gfx::textCentered("MUSIC",32,COL_ACCENT,2);
    for(int i=0;i<2;++i) {
        const int x=i?274:86;
        gfx::fillCircle(x,75,18,pressed==i+3?COL_ACCENT:COL_TRACK);
        gfx::text(i?"+":"-",x-3,71,pressed==i+3?COL_BG:COL_TEXT,1);
    }
    char text[36];snprintf(text,sizeof text,"VOL %d",volume);gfx::textCentered(text,50,COL_DIM,1);
    const int knob=audio::sliderForPercent(volume);
    gfx::fillRect(VOL_X0,VOL_Y-3,VOL_X1-VOL_X0,7,COL_TRACK);
    gfx::fillRect(VOL_X0,VOL_Y-3,(VOL_X1-VOL_X0)*knob/100,7,COL_ACCENT);
    gfx::fillCircle(VOL_X0+(VOL_X1-VOL_X0)*knob/100.0f,(float)VOL_Y,6.0f,COL_TEXT);
    if(!tracks) {
        gfx::textCentered(failed?"AUDIO UNAVAILABLE":"NO MUSIC",150,COL_TEXT,2);
        gfx::textCentered("put MP3 / WAV in /Music",195,COL_DIM,1);
        return;
    }
    gfx::fillCircle(180,145,49,COL_TRACK);
    for(int r=18;r<=46;r+=9)gfx::strokeCircle(180,145,r,1,PX(RGB565(0x43,0x34,0x3A)));
    gfx::fillCircle(180,145,16,COL_ACCENT);gfx::fillCircle(180,145,3,COL_BG);
    gfx::fillCircle(180+cosf(rotation)*29,145+sinf(rotation)*29,2,COL_TEXT);
    {
        const uint16_t c = deleteArmed ? COL_WARN : COL_DIM;
        const int lid = deleteArmed ? 10 : 8;
        gfx::fillRect(BIN_X - lid, BIN_Y - 9, 2 * lid, 2, c);
        gfx::fillRect(BIN_X - lid + 3, BIN_Y - 12, 2 * lid - 6, 2, c);
        gfx::fillRect(BIN_X - lid + 1, BIN_Y - 6, 2 * lid - 2, 12, c);
        if (deleteArmed) gfx::strokeCircle((float)BIN_X, (float)BIN_Y, 15.0f, 2.0f, COL_WARN);
    }
    gfx::textCentered(title,202,COL_TEXT,1);
    snprintf(text,sizeof text,"%02d / %02d",index+1,tracks);gfx::textCentered(text,218,COL_DIM,1);
    gfx::fillRect(66,242,228,3,COL_TRACK);
    if(duration)gfx::fillRect(66,242,int(228.0f*fminf(1,float(elapsed)/duration)),3,COL_ACCENT);
    snprintf(text,sizeof text,"%u:%02u",elapsed/60,elapsed%60);gfx::text(text,66,256,COL_DIM,1);
    snprintf(text,sizeof text,"%u:%02u",duration/60,duration%60);
    gfx::text(text,294-gfx::textWidth(text),256,COL_DIM,1);
    for(int i=0;i<3;++i) {
        const int x=110+i*70;const uint16_t c=pressed==i?COL_BG:COL_ACCENT;
        gfx::fillCircle(x,290,25,pressed==i?COL_ACCENT:COL_TRACK);
        if(i==1 && !paused && !failed) {
            gfx::fillRect(x-6,282,4,16,c);gfx::fillRect(x+2,282,4,16,c);
        } else {
            for(int row=-7;row<=7;++row) {
                const int w=9-abs(row);
                gfx::fillRect(i==0?x+4-w:x-4,290+row,w,1,c);
            }
            if(i!=1)gfx::fillRect(i==0?x-9:x+7,282,2,16,c);
        }
    }
    gfx::textCentered(failed?"CANNOT PLAY THIS FILE":paused?"PAUSED":"LOCAL MUSIC",334,COL_DIM,1);
}
void GameMusic::drawIcon(float cx,float cy,float size) const {
    gfx::fillCircle(cx,cy,size*.5f,accent());
    const uint16_t ink=PX(RGB565(0x4E,0x2E,0x23));
    const float x=cx-size*.14f,y=cy+size*.20f;
    gfx::fillCircle(x,y,size*.13f,ink);
    gfx::fillRect(int(x+size*.09f),int(cy-size*.27f),int(size*.07f)+1,int(size*.46f),ink);
    gfx::line(x+size*.14f,cy-size*.24f,x+size*.35f,cy-size*.08f,ink);
    gfx::line(x+size*.14f,cy-size*.20f,x+size*.35f,cy-size*.04f,ink);
}
