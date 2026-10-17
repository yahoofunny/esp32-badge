#include "charge_sleep.h"
#include <Arduino.h>
#include <Wire.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_lcd_panel_ops.h"
#include "Display_ST77916.h"
#include "gfx.h"
#include "power.h"
#include "sound.h"

extern esp_lcd_panel_handle_t panel_handle;
namespace charge_sleep {
[[noreturn]] void enter() {
    sound::stop();
    const auto battery=power::cached();
    gfx::clear(PX(RGB565(14,14,22)));
    const auto colour=PX(RGB565(230,180,80));
    gfx::textCentered("CHARGE ONLY",110,colour,2);
    char text[32];
    if (battery.valid) snprintf(text,sizeof(text),"%d%%",battery.percent);
    else snprintf(text,sizeof(text),"--");
    gfx::textCentered(text,155,PX(RGB565(236,236,244)),4);
    gfx::textCentered(power::stateText(battery.state),205,colour,1);
    gfx::textCentered("screen going to sleep",248,PX(RGB565(110,110,128)),1);
    gfx::textCentered("press BOOT to wake",276,colour,1);
    LCD_pushFull(fb);LCD_waitTrans();delay(1800);
    // The charger is before the board's power latch. Do not write the gauge's
    // shutdown or charger configuration: it must continue charging independently.
    Wire.beginTransmission(0x6b);Wire.write(0x08);Wire.write(0);Wire.endTransmission();
    Wire.beginTransmission(0x15);Wire.write(0xfe);Wire.write(0);Wire.endTransmission();
    // This launcher never initialises WiFi; returning from the voice firmware
    // already resets its connection. Deep sleep powers the radio down as well.
    pinMode(9,OUTPUT);digitalWrite(9,LOW);
    Set_Backlight(0);
    esp_lcd_panel_disp_on_off(panel_handle,false);
    ledcDetach(5);
    pinMode(5,OUTPUT);digitalWrite(5,LOW);
    pinMode(3,OUTPUT);digitalWrite(3,LOW);
    gpio_hold_en(GPIO_NUM_5);gpio_hold_en(GPIO_NUM_9);gpio_hold_en(GPIO_NUM_3);
    gpio_deep_sleep_hold_en();
    // BOOT is the available RTC-capable input; PWR belongs to the hardware latch.
    rtc_gpio_init(GPIO_NUM_0);
    rtc_gpio_set_direction(GPIO_NUM_0,RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(GPIO_NUM_0);rtc_gpio_pulldown_dis(GPIO_NUM_0);
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(GPIO_NUM_0,0));
    Serial.println("[power] charge-only deep sleep; BOOT wakes; hardware charger remains active");
    Serial.flush();
    esp_deep_sleep_start();
}
}
