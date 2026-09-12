#include <Arduino.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>

static void recorderFree(void*);
#define free recorderFree
#include "../../firmware/devices_badge/voice_recorder.h"
#undef free

FakeM5 M5;
static std::atomic<bool> allocationFailure{false},taskFailure{false};
static std::atomic<unsigned> allocations{0},frees{0};
static std::atomic<void*> allocation{nullptr};
static std::thread::id mainThread;
static bool callerOwnsAllocation=false;

struct FakeTask {
  std::mutex mutex;
  std::condition_variable changed;
  unsigned pending=0;
  bool shutdown=false;
  std::thread thread;
};
static thread_local FakeTask *currentTask=nullptr;
static std::vector<FakeTask*> tasks;
struct TaskStopped {};

int xTaskCreatePinnedToCore(void (*fn)(void*),const char*,uint32_t stack,void *ctx,int priority,TaskHandle_t *out,int core) {
  assert(stack==8192 && priority==1 && core==0);
  if(taskFailure.load())return 0;
  auto *task=new FakeTask;
  *out=task;tasks.push_back(task);
  task->thread=std::thread([=] {
    currentTask=task;
    try {fn(ctx);} catch(const TaskStopped&) {}
  });
  return pdPASS;
}
void xTaskNotifyGive(TaskHandle_t task) {
  std::lock_guard<std::mutex> lock(task->mutex);
  ++task->pending;task->changed.notify_one();
}
uint32_t ulTaskNotifyTake(int,uint32_t) {
  auto *task=currentTask;
  std::unique_lock<std::mutex> lock(task->mutex);
  task->changed.wait(lock,[=] {return task->pending || task->shutdown;});
  if(task->shutdown)throw TaskStopped{};
  auto count=task->pending;task->pending=0;return count;
}
void vTaskDelay(uint32_t value) {std::this_thread::sleep_for(std::chrono::milliseconds(value));}
uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t) {return 7000;}

void *heap_caps_calloc(size_t count,size_t size,int caps) {
  assert(std::this_thread::get_id()!=mainThread);
  assert(count==1 && size==960044 && caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(allocationFailure.load())return nullptr;
  assert(!allocation.load());
  void *ptr=std::calloc(count,size);
  allocation.store(ptr);++allocations;return ptr;
}
static void recorderFree(void *ptr) {
  assert(std::this_thread::get_id()!=mainThread || callerOwnsAllocation);
  assert(!M5.Mic.ownsBuffer.load());
  assert(allocation.load()==ptr);
  auto *bytes=static_cast<const uint8_t*>(ptr);
  for(size_t i=0;i<960044;i++)assert(bytes[i]==0);
  allocation.store(nullptr);++frees;std::free(ptr);
}

bool FakeMic::begin() {
  assert(std::this_thread::get_id()!=mainThread);
  ++begins;beginEntered.store(true);
  while(gateBegin.load())vTaskDelay(1);
  return !failBegin.load();
}
bool FakeMic::record(int16_t *data,size_t count,uint32_t rate,bool stereo) {
  assert(std::this_thread::get_id()!=mainThread);
  assert(count==800 && rate==16000 && !stereo);
  assert(!ownsBuffer.load());
  if(writer.joinable())writer.join();
  if(failRecord.load())return false;
  ++recordings;
  if(neverStarts.load())return true;
  ownsBuffer.store(true);
  writer=std::thread([=] {
    // Deliberately leave isRecording()==0 after enqueue, as the pinned Mic can.
    while(gateWrite.load())vTaskDelay(1);
    vTaskDelay(2);running.store(true);
    for(size_t i=0;i<count-1;i++)data[i]=(i%2)?-1000:2000;
    while(gateTail.load())vTaskDelay(1);
    data[count-1]=-1000;
    ownsBuffer.store(false);running.store(false);
  });
  return true;
}
void FakeMic::end() {
  assert(std::this_thread::get_id()!=mainThread);
  endEntered.store(true);
  if(writer.joinable())writer.join();
  assert(!ownsBuffer.load());++ends;
}
void FakeMic::reset() {
  assert(!ownsBuffer.load());
  if(writer.joinable())writer.join();
  failBegin=false;failRecord=false;neverStarts=false;
  gateBegin=false;beginEntered=false;gateWrite=false;gateTail=false;
  running=false;endEntered=false;recordings=0;begins=0;ends=0;
}

template<class Predicate> static void until(Predicate predicate,unsigned timeout=4000) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout);
  while(!predicate()) {
    assert(std::chrono::steady_clock::now()<deadline);
    vTaskDelay(1);
  }
}
template<class Action> static void nonblocking(Action action) {
  auto start=std::chrono::steady_clock::now();action();
  assert(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(20));
}
static uint32_t le32(const uint8_t *p) {return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
static uint16_t le16(const uint8_t *p) {return p[0]|uint16_t(p[1])<<8;}
static void checkWav(VoiceRecorder &rec,uint32_t expectedSamples=0) {
  uint8_t *wav=nullptr;size_t bytes=0;uint32_t duration=0;
  assert(rec.take(wav,bytes,duration));assert(wav && !M5.Mic.ownsBuffer.load());
  assert(M5.Mic.ends.load()==1 && !rec.busy() && !rec.recording());
  const uint32_t count=rec.sampleCount();
  if(expectedSamples)assert(count==expectedSamples);
  assert(count && count%800==0 && count<=480000);
  assert(bytes==44+count*2 && duration==count/16 && rec.durationMs()==duration);
  assert(!std::memcmp(wav,"RIFF",4) && le32(wav+4)==bytes-8);
  assert(!std::memcmp(wav+8,"WAVEfmt ",8) && le32(wav+16)==16);
  assert(le16(wav+20)==1 && le16(wav+22)==1 && le32(wav+24)==16000);
  assert(le32(wav+28)==32000 && le16(wav+32)==2 && le16(wav+34)==16);
  assert(!std::memcmp(wav+36,"data",4) && le32(wav+40)==count*2);
  assert(le16(wav+44)==2000 && int16_t(le16(wav+46))==-1000);
  assert(rec.peak()==2000 && rec.meanAbs()==1500);
  callerOwnsAllocation=true;std::memset(wav,0,bytes);recorderFree(wav);callerOwnsAllocation=false;
  wav=reinterpret_cast<uint8_t*>(1);bytes=1;duration=1;
  assert(!rec.take(wav,bytes,duration) && !wav && !bytes && !duration);
}

int main() {
  mainThread=std::this_thread::get_id();
  VoiceRecorder rec;
  assert(!rec.start() && rec.error()==VoiceRecorder::WORKER_UNAVAILABLE);
  taskFailure=true;assert(!rec.begin());taskFailure=false;
  assert(rec.begin() && rec.begin());
  assert(!rec.busy() && !rec.recording() && M5.Mic.begins==0); // No boot capture.

  allocationFailure=true;assert(rec.start());until([&] {return !rec.busy();});
  assert(rec.error()==VoiceRecorder::NO_MEMORY && !allocation.load());
  allocationFailure=false;rec.cancel();

  M5.Mic.reset();M5.Mic.failBegin=true;assert(rec.start());until([&] {return !rec.busy();});
  assert(rec.error()==VoiceRecorder::MIC_START_FAILED && !allocation.load());
  M5.Mic.reset();M5.Mic.failRecord=true;assert(rec.start());until([&] {return !rec.busy();});
  assert(rec.error()==VoiceRecorder::MIC_RECORD_FAILED && !allocation.load());

  M5.Mic.reset();M5.Mic.gateBegin=true;assert(rec.start());
  until([&] {return M5.Mic.beginEntered.load();});
  assert(!rec.recording());nonblocking([&] {rec.stop();});
  M5.Mic.gateBegin=false;until([&] {return !rec.busy();});
  assert(rec.error()==VoiceRecorder::NO_AUDIO && M5.Mic.recordings==0 && !allocation.load());

  M5.Mic.reset();M5.Mic.gateBegin=true;assert(rec.start());
  until([&] {return M5.Mic.beginEntered.load();});
  nonblocking([&] {rec.cancel();});assert(rec.busy());
  M5.Mic.gateBegin=false;until([&] {return !rec.busy();});
  assert(rec.error()==0 && M5.Mic.recordings==0 && !allocation.load());

  // False idle after enqueue cannot publish a live/unwritten buffer.
  M5.Mic.reset();M5.Mic.gateWrite=true;assert(rec.start());
  until([&] {return M5.Mic.ownsBuffer.load();});
  assert(rec.busy() && !rec.recording() && rec.sampleCount()==0);
  uint8_t *wav=nullptr;size_t bytes=0;uint32_t duration=0;
  assert(!rec.take(wav,bytes,duration));
  nonblocking([&] {rec.stop();});assert(rec.busy());
  M5.Mic.gateWrite=false;until([&] {return !rec.busy();});checkWav(rec,800);

  // Cancellation with a live partial write must retain its allocation until
  // Mic.end() has joined. Polling every public accessor stays nonblocking.
  M5.Mic.reset();M5.Mic.gateTail=true;assert(rec.start());
  until([&] {return rec.recording();});
  unsigned freed=frees.load();nonblocking([&] {rec.cancel();});
  until([&] {return M5.Mic.endEntered.load();});
  assert(M5.Mic.ownsBuffer && frees==freed && !rec.take(wav,bytes,duration));
  nonblocking([&] {assert(rec.busy());rec.error();rec.durationMs();rec.sampleCount();rec.peak();rec.meanAbs();});
  M5.Mic.gateTail=false;until([&] {return !rec.busy();});
  assert(frees==freed+1 && rec.error()==0 && !allocation.load());

  M5.Mic.reset();M5.Mic.neverStarts=true;assert(rec.start());
  until([&] {return !rec.busy();});
  assert(rec.error()==VoiceRecorder::MIC_TIMED_OUT && !rec.take(wav,bytes,duration) && !allocation.load());

  // A complete 30-second sample budget cannot overrun the single allocation.
  // The fake hardware runs faster than real time; this is not a timing test.
  M5.Mic.reset();assert(rec.start());until([&] {return !rec.busy();},10000);
  assert(M5.Mic.recordings==600);assert(!rec.start());checkWav(rec,480000);

  // A completed-but-untaken result is wiped by the worker, not by cancel().
  M5.Mic.reset();M5.Mic.gateWrite=true;assert(rec.start());
  until([&] {return M5.Mic.ownsBuffer.load();});rec.stop();M5.Mic.gateWrite=false;
  until([&] {return !rec.busy();});freed=frees;
  nonblocking([&] {rec.cancel();});assert(!rec.take(wav,bytes,duration));
  until([&] {return !rec.busy();});assert(frees==freed+1 && !allocation.load());

  // Repeated queued cancellation also catches publication/cancel races.
  for(int i=0;i<50;i++) {
    M5.Mic.reset();assert(rec.start());nonblocking([&] {rec.cancel();});
    until([&] {return !rec.busy();});assert(!rec.take(wav,bytes,duration) && !allocation.load());
  }
  assert(allocations==frees);
  for(auto *task:tasks) {
    {std::lock_guard<std::mutex> lock(task->mutex);task->shutdown=true;task->changed.notify_one();}
    task->thread.join();delete task;
  }
  std::cout<<"Voice recorder: WAV bounds, worker ownership, stop/cancel, startup failures, timeout, and 30-second cap passed.\n";
}
