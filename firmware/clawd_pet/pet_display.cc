#include "pet_display.h"
#include "gfx.h"
#include "input.h"
#include "application.h"
#include "lvgl_theme.h"
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "mcp_server.h"
#include "assets/lang_config.h"
#include <src/misc/cache/lv_cache.h>
#include <algorithm>
#include <cstring>

ClawdPetDisplay::ClawdPetDisplay(i2c_master_bus_handle_t bus, esp_lcd_panel_io_handle_t io,
        esp_lcd_panel_handle_t panel, int w, int h, int ox, int oy, bool mx, bool my, bool swap)
    : SpiLcdDisplay(io,panel,w,h,ox,oy,mx,my,swap) {
    clawd::BeginPlatform(bus);
    // The only imagery is our indexed clawd art; reserve PSRAM for audio instead.
    lvgl_port_lock(0); lv_image_cache_resize(64 * 1024, true); lvgl_port_unlock();
}
ClawdPetDisplay::~ClawdPetDisplay() {
    if(timer_)lv_timer_delete(timer_);
    heap_caps_free(front_); heap_caps_free(fb); fb=nullptr;
}
void ClawdPetDisplay::SetupUI() {
    if(setup_ui_called_)return;
    DisplayLockGuard lock(this);
    Display::SetupUI();
    const size_t bytes=SCR_W*SCR_H*sizeof(uint16_t);
    fb=static_cast<uint16_t*>(heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    front_=static_cast<uint16_t*>(heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!fb || !front_) { ESP_LOGE("pet","Cannot allocate pet buffers"); abort(); }
    auto screen=lv_screen_active();
    auto theme=static_cast<LvglTheme*>(current_theme_);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x0e0e16),0);
    lv_obj_set_style_text_font(screen,theme->text_font()->font(),0);
    image_desc_.header.magic=LV_IMAGE_HEADER_MAGIC;
    image_desc_.header.cf=LV_COLOR_FORMAT_RGB565;
    image_desc_.header.w=SCR_W;image_desc_.header.h=SCR_H;
    image_desc_.header.stride=SCR_W*2;
    image_desc_.data_size=bytes;image_desc_.data=reinterpret_cast<uint8_t*>(front_);
    image_=lv_image_create(screen);lv_image_set_src(image_,&image_desc_);lv_obj_set_pos(image_,0,0);
    status_label_=lv_label_create(screen);lv_obj_set_size(status_label_,200,19);
    lv_obj_set_pos(status_label_,80,5);lv_obj_set_style_text_align(status_label_,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(status_label_,lv_color_hex(0xaab0c0),0);
    lv_label_set_long_mode(status_label_,LV_LABEL_LONG_DOT);lv_label_set_text(status_label_,"CLAWD PET");
    notification_label_=lv_label_create(screen);lv_obj_set_size(notification_label_,200,19);
    lv_obj_set_pos(notification_label_,80,5);lv_obj_set_style_text_color(notification_label_,lv_color_white(),0);
    lv_obj_add_flag(notification_label_,LV_OBJ_FLAG_HIDDEN);
    subtitle_=lv_label_create(screen);lv_obj_set_size(subtitle_,248,68);lv_obj_set_pos(subtitle_,56,247);
    lv_obj_set_style_text_align(subtitle_,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(subtitle_,lv_color_hex(0xececf4),0);
    lv_obj_set_style_bg_color(subtitle_,lv_color_hex(0x171721),0);lv_obj_set_style_bg_opa(subtitle_,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(subtitle_,4,0);lv_obj_set_style_radius(subtitle_,12,0);
    lv_label_set_long_mode(subtitle_,LV_LABEL_LONG_SCROLL);lv_obj_add_flag(subtitle_,LV_OBJ_FLAG_HIDDEN);
    pet_.enter();last_tick_=uint32_t(esp_timer_get_time()/1000);
    McpServer::GetInstance().AddTool("self.pet.get_state",
        "Read the living CLAWD PET's name, evolution and needs. Call before answering questions about yourself, your mood or care.",
        PropertyList(), [this](const PropertyList&) -> ReturnValue {
            DisplayLockGuard lock(this); char context[512]; pet_.describe(context,sizeof(context));
            return std::string(context);
        });
    timer_=lv_timer_create([](lv_timer_t* t){static_cast<ClawdPetDisplay*>(lv_timer_get_user_data(t))->Tick();},clawd::UiPacing::kInputPollMs,this);
    ESP_LOGI("pet","CLAWD PET ready: 20ms touch polling; BOOT talks, hold returns");
}
void ClawdPetDisplay::Tick() {
    const int64_t started=esp_timer_get_time();
    const uint32_t now=uint32_t(started/1000);
    float dt=(now-last_tick_)/1000.f;last_tick_=now;
    dt=std::min(dt,1.f);
    clawd::PollInput();
    const int64_t sampled=esp_timer_get_time();
    const bool down=input::touchDown();
    const bool touch_event=down!=touch_was_down_ || input::tapped();
    // Wake on contact, not release. This resets only the display's idle timer;
    // don't reconfigure WiFi (and emit several console messages) for every tap.
    if(down && (!touch_was_down_ || sleeping_ || uint32_t(now-last_wake_)>=10000)) {
        if(wake_interaction_)wake_interaction_();
        last_wake_=now;
    }
    touch_was_down_=down;
    const auto state=Application::GetInstance().GetDeviceState();
    GamePet::Voice voice=GamePet::Voice::Idle;
    if(state==kDeviceStateListening)voice=GamePet::Voice::Listening;
    else if(state==kDeviceStateConnecting)voice=GamePet::Voice::Thinking;
    else if(state==kDeviceStateSpeaking)voice=GamePet::Voice::Speaking;
    pet_.setVoice(voice);
    const bool voice_changed=voice!=previous_voice_;previous_voice_=voice;
    // Header is a visible touch target as well as BOOT's existing chat action.
    if(input::tapped()) {
        if(input::tapY()<45)Application::GetInstance().ToggleChatState();
    }
    pet_.update(dt);
    const int64_t updated=esp_timer_get_time();
    const bool show=!pet_.wardrobeActive() && subtitle_until_ &&
        (int32_t(subtitle_until_-now)>0 || state==kDeviceStateActivating);
    const bool was_visible=!lv_obj_has_flag(subtitle_,LV_OBJ_FLAG_HIDDEN);
    if(show!=was_visible) {
        if(show)lv_obj_remove_flag(subtitle_,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(subtitle_,LV_OBJ_FLAG_HIDDEN);
    }
    uint32_t render_us=0,copy_us=0;
    const bool draw=pacing_.renderDue(now,down,touch_event||voice_changed,voice!=GamePet::Voice::Idle,sleeping_);
    if(draw) {
        const int64_t paint_started=esp_timer_get_time();
        pet_.render();
        const int64_t painted=esp_timer_get_time();
        // Copy only changed rows into the image's stable source and invalidate that band.
        int y0=SCR_H,y1=-1;
        for(int y=0;y<SCR_H;++y) {
            auto *src=fb+y*SCR_W,*dst=front_+y*SCR_W;
            if(memcmp(src,dst,SCR_W*2)) {memcpy(dst,src,SCR_W*2);y0=std::min(y0,y);y1=y;}
        }
        if(y1>=y0) {lv_area_t a{0,y0,SCR_W-1,y1};lv_obj_invalidate_area(image_,&a);}
        render_us=uint32_t(painted-paint_started);
        copy_us=uint32_t(esp_timer_get_time()-painted);
    }
    // Small aggregate diagnostics; touch edges and care actions stay at DEBUG
    // so USB console backpressure cannot add a blocking log burst to each tap.
    struct Perf {int64_t last=0;uint32_t since=0,polls=0,frames=0,gap=0,input=0,update=0,paint=0,copy=0;};
    static Perf perf;
    if(perf.last)perf.gap=std::max(perf.gap,uint32_t(started-perf.last));
    perf.last=started;if(!perf.since)perf.since=now;
    ++perf.polls;if(draw)++perf.frames;
    perf.input=std::max(perf.input,uint32_t(sampled-started));
    perf.update=std::max(perf.update,uint32_t(updated-sampled));
    perf.paint=std::max(perf.paint,render_us);perf.copy=std::max(perf.copy,copy_us);
    if(uint32_t(now-perf.since)>=10000) {
        ESP_LOGI("pet-perf","polls=%lu frames=%lu max_us gap=%lu input=%lu update=%lu paint=%lu copy=%lu sleep=%d",
            (unsigned long)perf.polls,(unsigned long)perf.frames,(unsigned long)perf.gap,
            (unsigned long)perf.input,(unsigned long)perf.update,(unsigned long)perf.paint,(unsigned long)perf.copy,sleeping_);
        perf.since=now;perf.polls=perf.frames=perf.gap=perf.input=perf.update=perf.paint=perf.copy=0;
    }
    if(input::bootDown() && !returning_) {
        // Button's long-press callback handles the switch; save before it can reboot.
        pet_.leave();returning_=true;
    } else if(!input::bootDown())returning_=false;
}
void ClawdPetDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);if(setup_ui_called_)pet_.react(emotion);
}
void ClawdPetDisplay::SetStatus(const char* status) {
    LvglDisplay::SetStatus(status && strcmp(status,Lang::Strings::STANDBY) ? status : "");
}
void ClawdPetDisplay::SetChatMessage(const char*,const char* content) {
    DisplayLockGuard lock(this);if(!subtitle_)return;
    lv_label_set_text(subtitle_,content?content:"");
    subtitle_until_=content&&*content?uint32_t(esp_timer_get_time()/1000)+15000:0;
}
void ClawdPetDisplay::ClearChatMessages(){SetChatMessage("system","");}
void ClawdPetDisplay::SetPowerSaveMode(bool on) {
    DisplayLockGuard lock(this);sleeping_=on;
}
void ClawdPetDisplay::SetTheme(Theme* theme) {
    // Assets may refresh a theme after network activation. The generic chat
    // display's containers don't exist here, so only update our own labels.
    DisplayLockGuard lock(this);current_theme_=theme;
    auto *t=static_cast<LvglTheme*>(theme);
    if(subtitle_)lv_obj_set_style_text_font(subtitle_,t->text_font()->font(),0);
}
