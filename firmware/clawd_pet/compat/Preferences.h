#pragma once
#include "nvs.h"
#include <cstddef>
// Same blob API and namespaces as Arduino Preferences, backed directly by IDF NVS.
class Preferences {
    nvs_handle_t handle_ = 0;
public:
    ~Preferences() { end(); }
    bool begin(const char* name, bool readOnly = false) {
        end();
        return nvs_open(name, readOnly ? NVS_READONLY : NVS_READWRITE, &handle_) == ESP_OK;
    }
    void end() { if (handle_) { nvs_close(handle_); handle_ = 0; } }
    bool isKey(const char* key) { size_t n = 0; return handle_ && nvs_get_blob(handle_, key, nullptr, &n) == ESP_OK; }
    size_t getBytes(const char* key, void* data, size_t capacity) {
        size_t n = capacity;
        return handle_ && nvs_get_blob(handle_, key, data, &n) == ESP_OK ? n : 0;
    }
    size_t putBytes(const char* key, const void* data, size_t n) {
        if (!handle_ || nvs_set_blob(handle_, key, data, n) != ESP_OK) return 0;
        return nvs_commit(handle_) == ESP_OK ? n : 0;
    }
};
