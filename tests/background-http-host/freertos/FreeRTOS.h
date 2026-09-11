#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>
struct FakeQueue {std::mutex lock;std::condition_variable ready;void *value=nullptr;};
using QueueHandle_t=FakeQueue*;
using TaskHandle_t=std::thread*;
using TickType_t=uint32_t;
constexpr int pdPASS=1,pdTRUE=1;
constexpr uint32_t portMAX_DELAY=UINT32_MAX;
namespace FakeRtos {
inline std::vector<FakeQueue*> queues;
inline std::thread *worker=nullptr;
inline bool shutdown=false;
struct Stop{};
inline void stop(){
  for(auto *queue:queues){std::lock_guard<std::mutex> hold(queue->lock);shutdown=true;queue->ready.notify_all();}
  if(worker){worker->join();delete worker;worker=nullptr;}
  for(auto *queue:queues)delete queue;
  queues.clear();
}
}
