#pragma once
#include <cstddef>
constexpr int MALLOC_CAP_SPIRAM=1;
constexpr int MALLOC_CAP_8BIT=2;
void *heap_caps_calloc(size_t count,size_t size,int caps);
