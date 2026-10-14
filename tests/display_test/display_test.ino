/*
 * Display path test.
 *
 * Fills the whole panel with a solid colour and cycles through white / red /
 * green / blue, one per second, printing each step on the serial console.
 *
 * This exists to bisect one question: is the display path itself alive, or is a
 * symptom "the screen is black" actually about something further up? A solid fill
 * has no sprites, no framebuffer tricks and no launcher -- if this shows colours but
 * a game does not, the fault is above the driver.
 */

#include <Arduino.h>
#include <Wire.h>

#include "Display_ST77916.h"
#include "I2C_Driver.h"
#include "esp_heap_caps.h"

static uint16_t *fb = nullptr;

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define PX(c) ((uint16_t)((((uint16_t)(c) & 0x00FF) << 8) | (((uint16_t)(c) & 0xFF00) >> 8)))

static void fill(uint16_t host565)
{
    const uint16_t v = PX(host565);
    for (int i = 0; i < 360 * 360; i++) {
        fb[i] = v;
    }
}

// A checkerboard, to prove that individual pixels arrive rather than the panel
// merely lighting up as a solid field.
static void checker(uint16_t a, uint16_t b, int cell)
{
    for (int y = 0; y < 360; y++) {
        for (int x = 0; x < 360; x++) {
            const bool on = ((x / cell) + (y / cell)) & 1;
            fb[y * 360 + x] = PX(on ? a : b);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    delay(600);
    Serial.println("\n[disp-test] starting");

    I2C_Init();

    fb = (uint16_t *)heap_caps_malloc(360 * 360 * 2, MALLOC_CAP_SPIRAM);
    Serial.printf("[disp-test] framebuffer at %p\n", fb);
    if (!fb) {
        Serial.println("[disp-test] FATAL: allocation failed");
        while (1) delay(1000);
    }

    Backlight_Init();
    Serial.println("[disp-test] backlight init done");

    LCD_Init();
    Serial.println("[disp-test] LCD_Init done");

    Set_Backlight(100);
    Serial.println("[disp-test] backlight driven to 100");
    Serial.println("[disp-test] the panel should now cycle white / red / green / blue");
}

void loop()
{
    static const uint16_t cycles[] = {0xFFFF, 0xF800, 0x07E0, 0x001F};
    static const char *names[] = {"white", "red", "green", "blue"};
    static int i = 0;

    Serial.printf("[disp-test] %s\n", names[i]);
    fill(cycles[i]);
    LCD_pushFull(fb);
    LCD_waitTrans();
    delay(1000);

    // a checkerboard between colours, so misdirected or duplicated pixels show up
    checker(0xFFFF, 0x0000, 24);
    LCD_pushFull(fb);
    LCD_waitTrans();
    delay(700);

    i = (i + 1) % 4;
}
