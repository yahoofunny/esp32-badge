// Executes the actual shared pet code with in-memory NVS; no board required.
#include <cassert>
#include <cstdio>
#include <vector>
#include <limits>
#include <cstring>
#include <cmath>
#include "native/Preferences.h"
#define private public
#include "../system/game_pet.h"
#undef private
#include "../system/input.h"
#include "../system/outfits.h"
#include "../system/assets_mon.h"

namespace sysclock { bool valid(){return false;} const char* timeText(){return "18:00";} }
namespace sound { void play2(int,int,int,int){} void nope(){} }
namespace input {
static bool down=false, released=false;
static int x=0,y=0;
static unsigned long held=0;
static float accel[3]={0.0f,0.0f,1.0f};
bool touchDown(){return down;} bool tapped(){return released;}
int tapX(){return x;} int tapY(){return y;} unsigned long touchHeldMs(){return held;}
void readRawAccel(float a[3]){memcpy(a,accel,sizeof(accel));}
}
static void touchFrame(GamePet& pet,bool down,bool released,int x,int y,unsigned long held=0) {
    input::down=down;input::released=released;input::x=x;input::y=y;input::held=held;
    pet.update(0);
    input::released=false;
}
static void tap(GamePet& pet,int x,int y) {
    touchFrame(pet,true,false,x,y);
    touchFrame(pet,false,true,x,y);
}
static void swipe(GamePet& pet,int startX,int endX,int y=184) {
    touchFrame(pet,true,false,startX,y);
    touchFrame(pet,true,false,endX,y,120);
    touchFrame(pet,false,true,endX,y);
}
static void openStyle(GamePet& pet) {
    float x,y;pet.menuSlot(mon::ACT_NAME,&x,&y);
    tap(pet,int(std::lround(x)),int(std::lround(y)));
    assert(pet.wardrobeActive());
}
template<class T> void put(const char* name,const T& value) {
    Preferences p;p.begin(name);p.putBytes("blob",&value,sizeof(value));
}
static void snapshot(GamePet& p,const char* path) {
    p.render();FILE* f=fopen(path,"wb");assert(f);
    fprintf(f,"P6\n360 360\n255\n");
    for(int y=0;y<SCR_H;++y)for(int x=0;x<SCR_W;++x){
        uint16_t c=PX(fb[y*SCR_W+x]);
        unsigned char px[]={static_cast<unsigned char>(((c>>11)&31)*255/31),
            static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};
        if((x-180)*(x-180)+(y-180)*(y-180)>180*180)memset(px,0,3);
        fwrite(px,1,3,f);
    }fclose(f);
}
static void interactionFeedbackChecks() {
    Preferences::blobs.clear();
    input::down=input::released=false;input::held=0;
    GamePet pet;pet.enter();
    pet.S.stage=mon::STAGE_CHILD;pet.S.form=mon::FORM_LEAF;
    pet.S.hunger=40;pet.S.happiness=40;pet.S.energy=70;pet.S.cleanliness=80;
    pet.S.feedCount=5;pet.S.flags=0;pet.thought[0]=0;pet.save();
    float fx,fy;pet.menuSlot(mon::ACT_FEED,&fx,&fy);
    const int x=int(std::lround(fx)),y=int(std::lround(fy));
    const int sample=y+10; // Inside the button fill, below the FEED glyphs.
    assert(pet.menuHit(x,y)==mon::ACT_FEED);
    pet.render();const uint16_t idlePixel=fb[sample*SCR_W+x];
    touchFrame(pet,true,false,x,y);
    pet.render();const uint16_t pressedPixel=fb[sample*SCR_W+x];
    auto brightness=[](uint16_t pixel) {
        const uint16_t rgb=PX(pixel);
        return int((rgb>>11)&31)+int((rgb>>5)&63)+int(rgb&31);
    };
    assert(brightness(pressedPixel)>brightness(idlePixel));
    assert(pet.S.hunger==40 && pet.S.happiness==40 && pet.S.feedCount==5);
    for(int i=0;i<20;++i) {
        input::held+=20;pet.update(.02f);
        assert(pet.S.hunger==40 && pet.S.happiness==40 && pet.S.feedCount==5);
    }
    assert(!pet.dragging);
    touchFrame(pet,false,true,x,y);
    assert(pet.S.hunger==70 && pet.S.happiness==50 && pet.S.feedCount==6);
    assert(pet.lastTapped==mon::ACT_FEED && pet.tapFlashSec>0);
    pet.render();assert(fb[sample*SCR_W+x]==pressedPixel);
    for(int i=0;i<7;++i) {
        pet.update(.02f);
        assert(pet.S.hunger==70 && pet.S.feedCount==6);
        assert(pet.lastTapped==mon::ACT_FEED);
    }
    pet.render();assert(fb[sample*SCR_W+x]==pressedPixel); // Still visible at 140 ms.
    pet.update(.02f);pet.render(); // Gone by 160 ms, independent of input scan rate.
    assert(pet.lastTapped==-1 && fb[sample*SCR_W+x]==idlePixel);
    for(int i=0;i<10;++i)pet.update(.02f);
    assert(pet.S.hunger==70 && pet.S.feedCount==6);

    pet.setFlag(GamePet::MF_ASLEEP,true);
    assert(pet.menuHit(x,y)==-1);
    pet.render();const uint16_t disabledPixel=fb[sample*SCR_W+x];
    const auto saveBefore=Preferences::blobs.at("clawdpet2/blob");
    touchFrame(pet,true,false,x,y);pet.render();
    assert(fb[sample*SCR_W+x]==disabledPixel);
    assert(pet.S.hunger==70 && pet.S.feedCount==6);
    touchFrame(pet,false,true,x,y);
    assert(pet.S.hunger==70 && pet.S.feedCount==6 && pet.has(GamePet::MF_ASLEEP));
    assert(pet.lastTapped==-1 && Preferences::blobs.at("clawdpet2/blob")==saveBefore);
    puts("PASS: press highlight without action, one feed on release, 150 ms feedback, no repeated feed, disabled asleep button");
}
static void accelFrame(GamePet& pet,float x,float y,float z,float dt=.06f) {
    input::accel[0]=x;input::accel[1]=y;input::accel[2]=z;
    pet.update(dt);
}
static void beginScrub(GamePet& pet,float x=0,float y=0,float z=1) {
    Preferences::blobs.clear();
    input::down=input::released=false;input::held=0;
    input::accel[0]=x;input::accel[1]=y;input::accel[2]=z;
    pet.enter();
    pet.S.stage=mon::STAGE_CHILD;pet.S.form=mon::FORM_LEAF;
    pet.S.hunger=70;pet.S.happiness=40;pet.S.energy=70;
    pet.S.cleanliness=40;pet.S.health=80;pet.S.flags=0;
    pet.save();
    float sx,sy;pet.menuSlot(mon::ACT_SCRUB,&sx,&sy);
    const int tx=int(std::lround(sx)),ty=int(std::lround(sy));
    assert(pet.menuHit(tx,ty)==mon::ACT_SCRUB);
    touchFrame(pet,true,false,tx,ty);
    assert(pet.mode==GamePet::MODE_MON);
    touchFrame(pet,false,true,tx,ty);
    assert(pet.mode==GamePet::MODE_SHAKE && pet.dirt==5);
    // The release that entered SCRUB must not also cancel it on the next scan.
    pet.update(0);
    assert(pet.mode==GamePet::MODE_SHAKE && pet.dirt==5);
    for(int i=0;i<6;++i)accelFrame(pet,x,y,z);
    assert(pet.mode==GamePet::MODE_SHAKE && pet.dirt==5);
}
static void assertCareSaved(const GamePet& pet) {
    GamePet restored;restored.enter();
    assert(std::fabs(restored.S.cleanliness-pet.S.cleanliness)<.001f);
    assert(std::fabs(restored.S.happiness-pet.S.happiness)<.001f);
}
static void naturalShakeChecks() {
    constexpr double pi=3.14159265358979323846;
    for(const int uiHz:{20,60}) {
        for(int axis=0;axis<3;++axis) {
            GamePet pet;beginScrub(pet);
            float sample[3]={0,0,1};
            double nextSample=.05;
            int frame=0;
            // The sensor publishes at 20 Hz; the faster UI only reads its cache.
            // A normal 2 Hz hand motion changes gradually, unlike square pulses.
            for(frame=1;frame<=uiHz*6 && pet.mode==GamePet::MODE_SHAKE;++frame) {
                const double now=double(frame)/uiHz;
                while(nextSample<=now+1e-7) {
                    sample[axis]=(axis==2?1.0f:0.0f)+
                        .45f*float(std::sin(4*pi*nextSample));
                    nextSample+=.05;
                }
                accelFrame(pet,sample[0],sample[1],sample[2],1.0f/uiHz);
            }
            assert(pet.mode==GamePet::MODE_MON && pet.dirt==0);
            assert(pet.S.cleanliness>90 && pet.S.happiness>49);
            printf("Natural SCRUB: axis %d, %d Hz UI / 20 Hz IMU, completed in %.2f s\n",
                axis,uiHz,double(frame-1)/uiHz);
        }

        GamePet slowTilt;beginScrub(slowTilt);
        float sample[3]={0,0,1};
        double nextSample=.05;
        for(int frame=1;frame<=uiHz*6;++frame) {
            const double now=double(frame)/uiHz;
            while(nextSample<=now+1e-7) {
                const double angle=nextSample*pi/6; // 30 degrees per second.
                sample[0]=float(std::sin(angle));sample[2]=float(std::cos(angle));
                nextSample+=.05;
            }
            accelFrame(slowTilt,sample[0],sample[1],sample[2],1.0f/uiHz);
            assert(slowTilt.mode==GamePet::MODE_SHAKE && slowTilt.dirt==5);
        }
    }
}
static void scrubChecks() {
    // Starting sideways must establish the current gravity vector as baseline.
    GamePet sideways;beginScrub(sideways,1,0,0);
    for(int i=0;i<12;++i)
        accelFrame(sideways,1+(i%2?.01f:-.01f),.01f,-.01f);
    assert(sideways.dirt==5);
    // A slow tilt changes direction without the acceleration of a shake.
    for(int i=1;i<=30;++i) {
        const float angle=i*.04f;
        accelFrame(sideways,std::cos(angle),0,std::sin(angle));
        assert(sideways.dirt==5);
    }
    tap(sideways,180,180);
    assert(sideways.mode==GamePet::MODE_MON);

    GamePet pet;beginScrub(pet);
    accelFrame(pet,.45f,0,1);assert(pet.dirt==4);
    // Cached IMU values may be read by several faster UI scans, never as new shakes.
    for(int i=0;i<15;++i) {
        accelFrame(pet,.45f,0,1,.02f);
        assert(pet.dirt==4);
    }
    // These two samples have equal magnitudes; their direction change must count.
    accelFrame(pet,-.45f,0,1);assert(pet.dirt==3);
    accelFrame(pet,.45f,0,1);assert(pet.dirt==3); // Within the 180 ms cooldown.
    accelFrame(pet,-.45f,0,1);assert(pet.dirt==3);
    for(int i=0;i<4;++i)accelFrame(pet,-.45f,0,1);
    assert(pet.dirt==3);
    accelFrame(pet,.45f,0,1);assert(pet.dirt==2);
    for(int i=0;i<4;++i)accelFrame(pet,.45f,0,1);
    accelFrame(pet,-.45f,0,1);assert(pet.dirt==1);
    for(int i=0;i<4;++i)accelFrame(pet,-.45f,0,1);
    // Let the same ordinary care clock advance without completing a bath, so the
    // reward assertion stays exact even when this final frame crosses a second.
    GamePet ordinary=pet;ordinary.setVoice(GamePet::Voice::Listening);
    ordinary.update(.06f);
    accelFrame(pet,.45f,0,1);
    assert(pet.mode==GamePet::MODE_MON && pet.dirt==0);
    assert(std::fabs(pet.S.cleanliness-(ordinary.S.cleanliness+55))<.001f);
    assert(std::fabs(pet.S.happiness-(ordinary.S.happiness+10))<.001f);
    assertCareSaved(pet);

    GamePet cancelled;beginScrub(cancelled);
    accelFrame(cancelled,.45f,0,1);
    for(int i=0;i<4;++i)accelFrame(cancelled,.45f,0,1);
    accelFrame(cancelled,-.45f,0,1);assert(cancelled.dirt==3);
    const float cleanBeforeCancel=cancelled.S.cleanliness;
    const float happyBeforeCancel=cancelled.S.happiness;
    tap(cancelled,180,180);
    assert(cancelled.mode==GamePet::MODE_MON && cancelled.dirt==0);
    assert(std::fabs(cancelled.S.cleanliness-(cleanBeforeCancel+22))<.001f);
    assert(cancelled.S.happiness==happyBeforeCancel);
    assertCareSaved(cancelled);

    GamePet timedOut;beginScrub(timedOut);
    accelFrame(timedOut,.45f,0,1);assert(timedOut.dirt==4);
    ordinary=timedOut;ordinary.setVoice(GamePet::Voice::Listening);
    ordinary.update(20.1f);
    accelFrame(timedOut,.45f,0,1,20.1f);
    assert(timedOut.mode==GamePet::MODE_MON && timedOut.dirt==0);
    assert(std::fabs(timedOut.S.cleanliness-(ordinary.S.cleanliness+11))<.001f);
    assert(std::fabs(timedOut.S.happiness-ordinary.S.happiness)<.001f);
    assertCareSaved(timedOut);

    GamePet speaking;beginScrub(speaking);
    speaking.setVoice(GamePet::Voice::Speaking);
    for(int i=0;i<12;++i)accelFrame(speaking,i%2?.45f:-.45f,0,1);
    tap(speaking,180,180);
    assert(speaking.mode==GamePet::MODE_SHAKE && speaking.dirt==5);
    speaking.setVoice(GamePet::Voice::Idle);
    tap(speaking,180,180);assert(speaking.mode==GamePet::MODE_MON);
    naturalShakeChecks();
    input::accel[0]=input::accel[1]=0;input::accel[2]=1;
    puts("PASS: SCRUB entry, gravity baseline, noise/tilt rejection, equal-magnitude shakes, cached samples, cooldown, complete/partial saved rewards, voice gating");
}
static void wardrobeChecks() {
    // Wardrobe preferences must not change either legacy care-save structure.
    static_assert(sizeof(GamePet::State)==80);
    static_assert(sizeof(GamePet::MonSave)==84);
    assert(outfits::count()>0 && outfits::count()<255);
    Preferences::blobs.clear();
    GamePet pet;pet.enter();pet.tick(180);pet.save();
    const auto careBefore=Preferences::blobs.at("clawdpet2/blob");
    const auto nameBefore=std::string(pet.S.name);
    assert(pet.outfit==0 && !pet.wardrobeActive());
    openStyle(pet);
    assert(pet.previewOutfit==0 && pet.S.name==nameBefore);
    snapshot(pet,"build/pet-wardrobe-original.ppm");
    const std::vector<uint16_t> originalBody(fb,fb+SCR_W*SCR_H);
    for(uint8_t style=1;style<=outfits::count();++style) {
        pet.previewOutfit=style;
        char path[80];snprintf(path,sizeof(path),"build/pet-wardrobe-style-%02u.ppm",style);
        snapshot(pet,path);
        for(int y=194;y<=214;++y)for(int x=162;x<=198;++x)
            assert(fb[y*SCR_W+x]==originalBody[y*SCR_W+x]);
    }
    pet.previewOutfit=0;
    tap(pet,37,184);assert(pet.previewOutfit==outfits::count());
    tap(pet,323,184);assert(pet.previewOutfit==0);
    swipe(pet,270,90);assert(pet.previewOutfit==1);
    swipe(pet,90,270);assert(pet.previewOutfit==0);
    swipe(pet,90,270);assert(pet.previewOutfit==outfits::count());
    swipe(pet,270,90);assert(pet.previewOutfit==0);
    // A vertical movement through the preview is neither an arrow nor a swipe.
    touchFrame(pet,true,false,180,145);
    touchFrame(pet,true,false,180,215,120);
    touchFrame(pet,false,true,180,215);
    assert(pet.previewOutfit==0);
    tap(pet,323,184);assert(pet.previewOutfit==1);
    tap(pet,108,283);
    assert(!pet.wardrobeActive() && pet.outfit==0);
    assert(!Preferences::blobs.count("clawdpet2/outfit"));
    assert(Preferences::blobs.at("clawdpet2/blob")==careBefore);
    openStyle(pet);assert(pet.previewOutfit==0);
    uint8_t chosen=1;
    for(uint8_t i=0;i<outfits::count();++i) {
        if(strstr(outfits::name(i),"SPROUT")) {chosen=i+1;break;}
    }
    while(pet.previewOutfit!=chosen)tap(pet,323,184);
    snapshot(pet,"build/pet-wardrobe.ppm");
    tap(pet,252,283);
    assert(!pet.wardrobeActive() && pet.outfit==chosen);
    assert(Preferences::blobs.at("clawdpet2/outfit")==std::vector<unsigned char>{chosen});
    assert(Preferences::blobs.at("clawdpet2/blob")==careBefore);
    snapshot(pet,"build/pet-outfit.ppm");
    GamePet restored;restored.enter();
    assert(restored.outfit==chosen && restored.previewOutfit==chosen);
    assert(restored.S.stage==pet.S.stage && restored.S.hunger==pet.S.hunger);

    // The voice screen hides the wardrobe; its underlying controls must not fire.
    openStyle(restored);tap(restored,323,184);
    const uint8_t preview=restored.previewOutfit;
    const auto persisted=Preferences::blobs.at("clawdpet2/outfit");
    restored.setVoice(GamePet::Voice::Listening);assert(!restored.wardrobeActive());
    tap(restored,252,283);tap(restored,323,184);swipe(restored,270,90);
    assert(restored.previewOutfit==preview && restored.outfit==chosen);
    assert(Preferences::blobs.at("clawdpet2/outfit")==persisted);
    restored.setVoice(GamePet::Voice::Idle);assert(restored.wardrobeActive());
    tap(restored,108,283);
    restored.setVoice(GamePet::Voice::Speaking);
    float styleX,styleY;restored.menuSlot(mon::ACT_NAME,&styleX,&styleY);
    tap(restored,int(std::lround(styleX)),int(std::lround(styleY)));
    assert(restored.mode==GamePet::MODE_MON && restored.outfit==chosen);
    restored.setVoice(GamePet::Voice::Idle);

    // Rename and a fresh egg remain accessible inside the wardrobe.
    openStyle(restored);
    const std::string oldName=restored.S.name;tap(restored,180,325);
    assert(restored.S.name!=oldName && restored.outfit==chosen);
    restored.S.stage=mon::STAGE_ADULT;restored.S.seen=0x17;
    tap(restored,180,325);
    assert(restored.S.stage==mon::STAGE_EGG && restored.S.seen==0x17);
    assert(restored.outfit==chosen);
    // Returning to the original body is persisted just like any other look.
    while(restored.previewOutfit!=0)tap(restored,323,184);
    tap(restored,252,283);assert(restored.outfit==0);
    GamePet original;original.enter();assert(original.outfit==0);
    Preferences::blobs["clawdpet2/outfit"]={255};
    GamePet invalid;invalid.enter();assert(invalid.outfit==0 && invalid.previewOutfit==0);
    Preferences::blobs["clawdpet2/outfit"]={chosen,chosen};
    GamePet malformed;malformed.enter();assert(malformed.outfit==0);
    puts("PASS: wardrobe taps/swipes, wrap, cancel/wear, independent save, restore, voice gating, rename/new egg");
}
static void careAccessoryPreviews() {
    GamePet pet;pet.enter();pet.mode=GamePet::MODE_MON;
    pet.setVoice(GamePet::Voice::Idle);
    pet.monX=180;pet.monY=184;pet.lookX=pet.lookY=0;
    pet.anim=mon::ANIM_IDLE;pet.animFrame=0;pet.bobPhase=0;pet.jitter=0;
    pet.S.flags=0;pet.S.hunger=81;pet.S.happiness=75;pet.S.energy=72;
    pet.S.cleanliness=86;pet.S.health=78;
    for(int adult=0;adult<=1;++adult) {
        pet.S.stage=adult?mon::STAGE_ADULT:mon::STAGE_CHILD;
        pet.S.form=adult?mon::FORM_BLOOM:mon::FORM_LEAF;
        strcpy(pet.S.name,adult?"BLOOM":"LEAF");
        const auto& form=mon_forms[pet.S.form];
        for(uint8_t accessory=0;accessory<outfits::count();++accessory) {
            const char* label=outfits::name(accessory);
            const bool hat=!strcmp(outfits::name(accessory),"HARD HAT");
            const bool headphones=!strcmp(outfits::name(accessory),"HEADPHONES");
            const bool flower=!strcmp(label,"FLOWER");
            const bool wizard=!strcmp(label,"WIZARD");
            if(!hat && !headphones && !flower && !wizard)continue;
            const char* slug=hat?"hat":headphones?"headphones":flower?"flower":"wizard";
            pet.mode=GamePet::MODE_MON;
            pet.outfit=accessory+1;pet.outfitMs=0;pet.thought[0]=0;
            pet.render();const std::vector<uint16_t> noThought(fb,fb+SCR_W*SCR_H);
            gfx::clear(0);
            assert(outfits::draw(accessory,0,180,184,form.w,form.h));
            const std::vector<uint16_t> accessoryMask(fb,fb+SCR_W*SCR_H);
            pet.say("mmm, thanks!");
            char path[96];snprintf(path,sizeof(path),"build/pet-care-%s-%s.ppm",
                adult?"adult":"child",slug);
            snapshot(pet,path);
            unsigned covered=0;
            for(int i=0;i<SCR_W*SCR_H;++i)
                if(accessoryMask[i] && fb[i]!=noThought[i])++covered;
            printf("Care accessory layout: %s %s, thought covers %u accessory pixels\n",
                adult?"adult":"child",outfits::name(accessory),covered);
            pet.mode=GamePet::MODE_WARDROBE;pet.previewOutfit=pet.outfit;
            gfx::clear(0);
            assert(outfits::draw(accessory,0,180,204,form.w,form.h));
            const std::vector<uint16_t> wardrobeMask(fb,fb+SCR_W*SCR_H);
            snprintf(path,sizeof(path),"build/pet-wardrobe-%s-%s.ppm",adult?"adult":"child",slug);
            snapshot(pet,path);
            const int titleScale=gfx::textWidth(label,2)<=270?2:1;
            const int titleWidth=gfx::textWidth(label,titleScale);
            unsigned titleOverlap=0;
            int top=SCR_H,bottom=-1;
            for(int y=0;y<SCR_H;++y)for(int x=0;x<SCR_W;++x)
                if(wardrobeMask[y*SCR_W+x]){if(y<top)top=y;if(y>bottom)bottom=y;}
            for(int y=86;y<86+8*titleScale;++y)
                for(int x=180-titleWidth/2;x<180+(titleWidth+1)/2;++x)
                    if(wardrobeMask[y*SCR_W+x])++titleOverlap;
            printf("Wardrobe accessory layout: %s %s, accessory y=%d..%d, %u pixels inside title bounds\n",
                adult?"adult":"child",label,top,bottom,titleOverlap);
        }
    }
}
int main(){
    std::vector<uint16_t> pixels(SCR_W*SCR_H);fb=pixels.data();
    GamePet pet;pet.enter();assert(pet.S.stage==mon::STAGE_EGG);
    pet.tick(180);assert(pet.S.stage==mon::STAGE_BABY);
    strcpy(pet.S.name,"clawd");pet.S.hunger=75;pet.S.seen=0x17;
    GamePet::MonSave monSave{0x4d4f4e31,pet.S};
    Preferences::blobs.clear();put("pixmon",monSave);
    GamePet merged;merged.enter();
    assert(merged.S.stage==mon::STAGE_BABY && merged.S.hunger==75 && merged.S.seen==0x17);
    assert(Preferences::blobs.count("clawdpet2/blob") && Preferences::blobs.count("pixmon/blob"));
    const auto old=Preferences::blobs.at("pixmon/blob");
    merged.S.hunger=88;merged.save();GamePet again;again.enter();assert(again.S.hunger==88);
    assert(Preferences::blobs.at("pixmon/blob")==old);
    Preferences::blobs.clear();
    struct OldState{uint32_t age,epoch,ms,session;float hunger,happy,energy,clean,health,care;
        uint8_t stage,flags;uint16_t feeds,attentions;char name[12];};
    struct OldSave{uint32_t magic;OldState state;} legacy{0x50544544,{}};
    legacy.state.hunger=81;legacy.state.happy=70;legacy.state.energy=60;legacy.state.clean=90;
    legacy.state.health=75;legacy.state.care=10;legacy.state.stage=3;strcpy(legacy.state.name,"old-clawd");
    static_assert(sizeof(OldSave)==64);put("clawdpet",legacy);
    GamePet migrated;migrated.enter();assert(migrated.S.stage==mon::STAGE_ADULT);
    assert(migrated.S.hunger==81 && !strcmp(migrated.S.name,"old-clawd"));
    assert(Preferences::blobs.count("clawdpet/blob"));
    snapshot(migrated,"build/pet-unified.ppm");
    migrated.setVoice(GamePet::Voice::Listening);snapshot(migrated,"build/pet-listening.ppm");
    migrated.setVoice(GamePet::Voice::Speaking);migrated.bobPhase=.4f;snapshot(migrated,"build/pet-speaking.ppm");
    const auto hunger=migrated.S.hunger;migrated.react("happy");assert(migrated.S.hunger==hunger);
    Preferences::blobs.clear();monSave.state.energy=std::numeric_limits<float>::quiet_NaN();
    monSave.state.seen=255;put("pixmon",monSave);
    GamePet corrupt;corrupt.enter();assert(corrupt.S.stage==mon::STAGE_EGG && corrupt.sane() && corrupt.S.seen==0);
    wardrobeChecks();
    careAccessoryPreviews();
    interactionFeedbackChecks();
    scrubChecks();
    puts("PASS: egg hatch, both legacy migrations, canonical save priority, corruption, voice presentation");
}
