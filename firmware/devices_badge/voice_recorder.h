#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <atomic>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// One lifetime instance; public methods are called only by the main task.
// Only this worker uses M5.Mic. Starting the worker does not enable recording.
// A successful take() transfers the allocation to the caller, which must wipe
// its returned byte length before free(). There is no audio/file/serial output.
class VoiceRecorder {
 public:
  static constexpr uint32_t SAMPLE_RATE=16000;
  static constexpr uint32_t MAX_DURATION_MS=30000;
  static constexpr size_t MAX_SAMPLES=480000;
  static constexpr size_t WAV_HEADER_BYTES=44;
  static constexpr size_t MAX_WAV_BYTES=WAV_HEADER_BYTES+MAX_SAMPLES*sizeof(int16_t);
  static constexpr size_t CHUNK_SAMPLES=800; // even, 50 ms at 16 kHz

  enum Error : int {
    OK=0, WORKER_UNAVAILABLE=1, NO_MEMORY=2, MIC_START_FAILED=3,
    MIC_RECORD_FAILED=4, MIC_TIMED_OUT=5, NO_AUDIO=6
  };

  VoiceRecorder()=default;
  VoiceRecorder(const VoiceRecorder&)=delete;
  VoiceRecorder& operator=(const VoiceRecorder&)=delete;

  bool begin() {
    if(worker_)return true;
    if(xTaskCreatePinnedToCore(workerEntry,"badge_mic",8192,this,1,&worker_,0)!=pdPASS) {
      worker_=nullptr;error_.store(WORKER_UNAVAILABLE);return false;
    }
    return true;
  }

  bool start() {
    if(!worker_) {error_.store(WORKER_UNAVAILABLE);return false;}
    State state=state_.load(std::memory_order_acquire);
    if(state!=State::IDLE && state!=State::FAILED)return false;
    // The idle/failed worker cannot modify these fields. Completed recordings
    // must be taken or cancelled before another capture can be accepted.
    cancelRequested_.store(false);
    stopRequested_.store(false);
    sampleCount_.store(0);peak_.store(0);meanAbs_.store(0);
    error_.store(OK);
    state_.store(State::QUEUED,std::memory_order_release);
    xTaskNotifyGive(worker_);
    return true;
  }

  // Stop finalizes the current short chunk; cancel discards the whole capture.
  // Both return immediately, including while microphone startup is in progress.
  void stop() {
    if(busy())stopRequested_.store(true,std::memory_order_release);
  }

  void cancel() {
    State state=state_.load(std::memory_order_acquire);
    if(state==State::IDLE)return;
    if(state==State::FAILED) {
      error_.store(OK);state_.store(State::IDLE,std::memory_order_release);return;
    }
    cancelRequested_.store(true,std::memory_order_release);
    stopRequested_.store(true,std::memory_order_release);
    // The worker also owns a completed result until take(), so cancellation
    // never wipes a large PSRAM allocation on the display task.
    if(worker_)xTaskNotifyGive(worker_);
  }

  bool busy() const {
    State state=state_.load(std::memory_order_acquire);
    return state==State::QUEUED || state==State::CAPTURING || state==State::DISCARDING ||
      (state==State::READY && cancelRequested_.load(std::memory_order_acquire));
  }
  bool recording() const {return recording_.load(std::memory_order_acquire);}
  uint32_t durationMs() const {return sampleCount()*1000u/SAMPLE_RATE;}
  uint32_t sampleCount() const {return sampleCount_.load(std::memory_order_acquire);}
  // Peak and mean absolute amplitude are published after microphone shutdown.
  uint32_t peak() const {return peak_.load(std::memory_order_acquire);}
  uint32_t meanAbs() const {return meanAbs_.load(std::memory_order_acquire);}
  int error() const {return error_.load(std::memory_order_acquire);}
  uint32_t workerStackFree() const {
    return worker_?uint32_t(uxTaskGetStackHighWaterMark(worker_)):0;
  }

  bool take(uint8_t *&wav,size_t &bytes,uint32_t &duration) {
    wav=nullptr;bytes=0;duration=0;
    if(cancelRequested_.load(std::memory_order_acquire))return false;
    State expected=State::READY;
    if(!state_.compare_exchange_strong(expected,State::CLAIMED,std::memory_order_acq_rel))return false;
    wav=buffer_;bytes=resultBytes_;duration=durationMs();
    buffer_=nullptr;resultBytes_=0;
    state_.store(State::IDLE,std::memory_order_release);
    return true;
  }

 private:
  enum class State : uint8_t {IDLE,QUEUED,CAPTURING,READY,CLAIMED,DISCARDING,FAILED};
  static constexpr uint32_t CHUNK_TIMEOUT_MS=2000;
  // The pinned M5Unified 0.2.19 writer saturates to [INT16_MIN+16,
  // INT16_MAX-16]. This impossible sample distinguishes a newly enqueued chunk
  // from an already finished one: isRecording() alone can initially be zero.
  static constexpr int16_t UNWRITTEN=INT16_MIN;
  static_assert(CHUNK_SAMPLES%2==0 && MAX_SAMPLES%CHUNK_SAMPLES==0,"even complete mic chunks required");
  static_assert(sizeof(int16_t)==2 && WAV_HEADER_BYTES%alignof(int16_t)==0,"PCM16 alignment required");

  static void workerEntry(void *context) {
    auto *self=static_cast<VoiceRecorder*>(context);
    for(;;) {
      ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
      State state=self->state_.load(std::memory_order_acquire);
      if(state==State::QUEUED)self->capture();
      // A cancellation can arrive just as capture publishes READY. Its pending
      // notification plus this check ensures that late result is also scrubbed.
      state=self->state_.load(std::memory_order_acquire);
      if(state==State::READY && self->cancelRequested_.load(std::memory_order_acquire)) {
        State expected=State::READY;
        if(self->state_.compare_exchange_strong(expected,State::DISCARDING,std::memory_order_acq_rel)) {
          self->discardBuffer();self->error_.store(OK);
          self->state_.store(State::IDLE,std::memory_order_release);
        }
      }
    }
  }

  static void put16(uint8_t *out,uint16_t value) {
    out[0]=uint8_t(value);out[1]=uint8_t(value>>8);
  }
  static void put32(uint8_t *out,uint32_t value) {
    for(size_t i=0;i<4;i++)out[i]=uint8_t(value>>(i*8));
  }
  static void wavHeader(uint8_t *out,size_t samples) {
    const uint32_t dataBytes=uint32_t(samples*sizeof(int16_t));
    memcpy(out,"RIFF",4);put32(out+4,36+dataBytes);memcpy(out+8,"WAVEfmt ",8);
    put32(out+16,16);put16(out+20,1);put16(out+22,1);
    put32(out+24,SAMPLE_RATE);put32(out+28,SAMPLE_RATE*sizeof(int16_t));
    put16(out+32,sizeof(int16_t));put16(out+34,16);
    memcpy(out+36,"data",4);put32(out+40,dataBytes);
  }
  void discardBuffer() {
    if(buffer_) {
      volatile uint8_t *out=buffer_;
      for(size_t i=0;i<MAX_WAV_BYTES;i++)out[i]=0;
      free(buffer_);buffer_=nullptr;
    }
    resultBytes_=0;
  }
  void finishFailure(int code) {
    state_.store(State::DISCARDING,std::memory_order_release);
    discardBuffer();
    if(cancelRequested_.load(std::memory_order_acquire)) {
      error_.store(OK);state_.store(State::IDLE,std::memory_order_release);
    } else {
      error_.store(code);state_.store(State::FAILED,std::memory_order_release);
    }
  }

  void capture() {
    state_.store(State::CAPTURING,std::memory_order_release);
    if(cancelRequested_.load(std::memory_order_acquire)) {finishFailure(OK);return;}
    if(stopRequested_.load(std::memory_order_acquire)) {finishFailure(NO_AUDIO);return;}
    buffer_=static_cast<uint8_t*>(heap_caps_calloc(1,MAX_WAV_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!buffer_) {finishFailure(NO_MEMORY);return;}
    if(cancelRequested_.load(std::memory_order_acquire)) {finishFailure(OK);return;}

    auto cfg=M5.Mic.config();
    cfg.sample_rate=SAMPLE_RATE;
    cfg.over_sampling=1;
    cfg.stereo=false;
    // Keep the board's I2S pins, channel selection, and codec callback. Pin both
    // audio tasks to the same core; the display task never reads sample memory.
    cfg.task_pinned_core=0;
    M5.Mic.config(cfg);
    if(!M5.Mic.begin()) {
      M5.Mic.end();finishFailure(MIC_START_FAILED);return;
    }

    size_t completed=0;
    int failure=OK;
    auto *samples=reinterpret_cast<int16_t*>(buffer_+WAV_HEADER_BYTES);
    while(completed<MAX_SAMPLES && !stopRequested_.load(std::memory_order_acquire) &&
          !cancelRequested_.load(std::memory_order_acquire)) {
      auto *tail=reinterpret_cast<volatile int16_t*>(samples+completed+CHUNK_SAMPLES-1);
      *tail=UNWRITTEN;
      if(!M5.Mic.record(samples+completed,CHUNK_SAMPLES,SAMPLE_RATE,false)) {
        failure=MIC_RECORD_FAILED;break;
      }
      const uint32_t started=millis();
      bool chunkComplete=false;
      while(uint32_t(millis()-started)<CHUNK_TIMEOUT_MS) {
        if(M5.Mic.isRecording())recording_.store(true,std::memory_order_release);
        if(*tail!=UNWRITTEN && !M5.Mic.isRecording()) {chunkComplete=true;break;}
        if(cancelRequested_.load(std::memory_order_acquire))break;
        vTaskDelay(1);
      }
      if(chunkComplete) {
        completed+=CHUNK_SAMPLES;
        sampleCount_.store(uint32_t(completed),std::memory_order_release);
      } else if(!cancelRequested_.load(std::memory_order_acquire)) {
        failure=MIC_TIMED_OUT;break;
      }
    }

    // Mic.end() joins the library's task before we inspect, wipe, free, or
    // publish the audio. No queued chunk is longer than 50 ms of samples, but
    // faulty I2S reads can make the pinned library's join much longer. Retain
    // ownership in that case; never force-delete the mic task or free its buffer.
    M5.Mic.end();
    recording_.store(false,std::memory_order_release);
    if(cancelRequested_.load(std::memory_order_acquire)) {finishFailure(OK);return;}
    if(failure!=OK || !completed) {finishFailure(failure!=OK?failure:NO_AUDIO);return;}

    uint32_t peak=0;
    uint64_t sum=0;
    for(size_t i=0;i<completed;i++) {
      const int32_t value=samples[i];
      const uint32_t magnitude=value<0?uint32_t(-value):uint32_t(value);
      if(magnitude>peak)peak=magnitude;
      sum+=magnitude;
    }
    peak_.store(peak,std::memory_order_release);
    meanAbs_.store(uint32_t(sum/completed),std::memory_order_release);
    wavHeader(buffer_,completed);
    resultBytes_=WAV_HEADER_BYTES+completed*sizeof(int16_t);
    error_.store(OK);
    state_.store(State::READY,std::memory_order_release);
  }

  TaskHandle_t worker_=nullptr;
  std::atomic<State> state_{State::IDLE};
  std::atomic<bool> stopRequested_{false},cancelRequested_{false},recording_{false};
  std::atomic<uint32_t> sampleCount_{0},peak_{0},meanAbs_{0};
  std::atomic<int> error_{OK};
  uint8_t *buffer_=nullptr;
  size_t resultBytes_=0;
};
