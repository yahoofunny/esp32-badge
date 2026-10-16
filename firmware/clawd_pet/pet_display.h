#pragma once
#include "display/lcd_display.h"
#include "pet_platform.h"
#include "game_pet.h"
#include "pet_ui_pacing.h"
#include <functional>
#include <string>

class ClawdPetDisplay : public SpiLcdDisplay {
public:
    ClawdPetDisplay(i2c_master_bus_handle_t bus, esp_lcd_panel_io_handle_t io,
        esp_lcd_panel_handle_t panel, int w, int h, int ox, int oy, bool mx, bool my, bool swap);
    ~ClawdPetDisplay() override;
    void SetupUI() override;
    void SetEmotion(const char* emotion) override;
    void SetStatus(const char* status) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void SetPowerSaveMode(bool on) override;
    void SetTheme(Theme* theme) override;
    void SetInteractionCallback(std::function<void()> callback) { wake_interaction_ = callback; }
private:
    GamePet pet_;
    lv_obj_t *image_ = nullptr, *subtitle_ = nullptr;
    lv_timer_t *timer_ = nullptr;
    lv_image_dsc_t image_desc_{};
    uint16_t *front_ = nullptr;
    uint32_t last_tick_ = 0, subtitle_until_ = 0;
    bool sleeping_ = false;
    bool returning_ = false;
    bool touch_was_down_ = false;
    uint32_t last_wake_ = 0;
    GamePet::Voice previous_voice_ = GamePet::Voice::Idle;
    clawd::UiPacing pacing_;
    std::function<void()> wake_interaction_;
    void Tick();
};
