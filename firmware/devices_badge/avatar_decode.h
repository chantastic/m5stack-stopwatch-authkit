#pragma once

#include <stddef.h>
#include <stdint.h>
#include <esp_heap_caps.h>

// X profile images may be progressive JPEGs, which M5GFX's tiny JPEG decoder
// does not support. Keep this decoder private to the sketch's translation unit.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS 512
#define STBI_MALLOC(size) heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_REALLOC(pointer, size) heap_caps_realloc((pointer), (size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define STBI_FREE(pointer) heap_caps_free(pointer)
#include "vendor/stb_image.h"
#undef STBI_FREE
#undef STBI_REALLOC
#undef STBI_MALLOC
#undef STBI_MAX_DIMENSIONS
#undef STBI_NO_FAILURE_STRINGS
#undef STBI_NO_THREAD_LOCALS
#undef STBI_NO_SIMD
#undef STBI_NO_LINEAR
#undef STBI_NO_HDR
#undef STBI_NO_STDIO
#undef STBI_ONLY_JPEG
#undef STB_IMAGE_IMPLEMENTATION
#undef STB_IMAGE_STATIC

// The caller owns the returned packed RGB888 pixels. No network or file I/O.
// Failure always leaves the dimensions empty; the caller can retain a fallback.
static uint8_t *badgeDecodeAvatarJpeg(const uint8_t *data, size_t length,
                                    int &width, int &height) {
  width = 0;
  height = 0;
  if (!data || length < 4 || length > 128 * 1024 ||
      data[0] != 0xff || data[1] != 0xd8) return nullptr;

  int imageWidth = 0, imageHeight = 0, channels = 0;
  if (!stbi_info_from_memory(data, static_cast<int>(length), &imageWidth,
                             &imageHeight, &channels) ||
      imageWidth < 1 || imageHeight < 1 || imageWidth > 512 || imageHeight > 512)
    return nullptr;

  uint8_t *pixels = stbi_load_from_memory(data, static_cast<int>(length),
                                         &imageWidth, &imageHeight, &channels, 3);
  if (!pixels) return nullptr;
  width = imageWidth;
  height = imageHeight;
  return pixels;
}

static void badgeFreeAvatarPixels(uint8_t *pixels) {
  stbi_image_free(pixels);
}
