#pragma once
// A small source-compatibility layer, not the Arduino runtime.
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
inline uint32_t millis() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
inline long random(long lo, long hi) { return hi > lo ? lo + esp_random() % (hi - lo) : lo; }
struct PetLog {
    template<class... Args> void printf(const char* fmt, Args... args) {
        if (esp_log_level_get("pet-model") >= ESP_LOG_DEBUG)
            esp_log_write(ESP_LOG_DEBUG, "pet-model", fmt, args...);
    }
    void println(const char* text) {
        if (esp_log_level_get("pet-model") >= ESP_LOG_DEBUG)
            esp_log_write(ESP_LOG_DEBUG, "pet-model", "%s\n", text);
    }
};
inline PetLog Serial;
