#pragma once
#include "FreeRTOS.h"
#include <atomic>
namespace FakeRtos {inline std::atomic<bool> rejectNextSend{false};}
inline QueueHandle_t xQueueCreate(int,size_t){auto *q=new FakeQueue;FakeRtos::queues.push_back(q);return q;}
inline int xQueueSend(FakeQueue *q,void *pointer,uint32_t wait){
  std::unique_lock<std::mutex> hold(q->lock);
  if(FakeRtos::rejectNextSend.exchange(false))return 0;
  if(wait==portMAX_DELAY)q->ready.wait(hold,[&]{return !q->value||FakeRtos::shutdown;});
  if(FakeRtos::shutdown)throw FakeRtos::Stop{};
  if(q->value)return 0;
  std::memcpy(&q->value,pointer,sizeof(void*));q->ready.notify_all();return 1;
}
inline int xQueueReceive(FakeQueue *q,void *pointer,uint32_t wait){std::unique_lock<std::mutex> hold(q->lock);if(wait==portMAX_DELAY)q->ready.wait(hold,[&]{return q->value||FakeRtos::shutdown;});if(FakeRtos::shutdown)throw FakeRtos::Stop{};if(!q->value)return 0;std::memcpy(pointer,&q->value,sizeof(void*));q->value=nullptr;q->ready.notify_all();return 1;}
inline void vQueueDelete(FakeQueue*){}
