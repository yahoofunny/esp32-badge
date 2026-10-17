#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
using boolean=bool;
inline bool psramFound(){return true;}
inline void *ps_malloc(size_t n){return malloc(n);}
inline void *ps_calloc(size_t n,size_t s){return calloc(n,s);}
inline void *heap_caps_malloc_prefer(size_t n,int,...){return malloc(n);}
constexpr int MALLOC_CAP_DEFAULT=0,MALLOC_CAP_SPIRAM=0,MALLOC_CAP_INTERNAL=0;
#define log_e(...) ((void)0)
#define log_i(...) ((void)0)
#define log_w(...) ((void)0)
inline void vTaskDelay(int){abort();} // The bit reader must never underrun.
