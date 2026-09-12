#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

struct FakeMicConfig {
  uint32_t sample_rate=16000;
  uint8_t over_sampling=1;
  bool stereo=false;
  uint8_t task_pinned_core=0;
};
struct FakeMic {
  FakeMicConfig cfg;
  std::atomic<bool> failBegin{false},failRecord{false},neverStarts{false};
  std::atomic<bool> gateBegin{false},beginEntered{false},gateWrite{false},gateTail{false};
  std::atomic<bool> ownsBuffer{false},running{false},endEntered{false};
  std::atomic<unsigned> recordings{0},begins{0},ends{0};
  std::thread writer;
  FakeMicConfig config() const {return cfg;}
  void config(const FakeMicConfig &value) {cfg=value;}
  bool begin();
  bool record(int16_t*,size_t,uint32_t,bool);
  size_t isRecording() const {return running.load()?1:0;}
  void end();
  void reset();
};
struct FakeM5 {FakeMic Mic;};
extern FakeM5 M5;
