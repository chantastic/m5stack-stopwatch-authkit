#pragma once
#include <chrono>
#include <cstdint>
#include <cstdlib>
inline uint32_t millis() {
  static const auto origin=std::chrono::steady_clock::now();
  return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-origin).count());
}
