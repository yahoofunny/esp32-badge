#pragma once
#include <stdint.h>

namespace clawd_album {
struct Entry {
    const char *name;
    const uint8_t *data;
    uint32_t size;
    uint32_t crc32;
    uint16_t contentX,contentY,contentW,contentH;
};
int count();
const Entry *at(int index);
}
