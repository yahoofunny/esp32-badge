#pragma once
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
inline uint32_t testMillis = 10000;
inline uint32_t millis() { return testMillis; }
inline long random(long lo, long) { return lo; }
struct NativeLog {
    template<class... A> void printf(const char*, A...) {}
    void println(const char*) {}
};
inline NativeLog Serial;
