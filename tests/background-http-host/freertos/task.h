#pragma once
#include "FreeRTOS.h"
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,int,void *ctx,int,TaskHandle_t *out,int){FakeRtos::worker=new std::thread([=]{try{fn(ctx);}catch(const FakeRtos::Stop&) {}});*out=FakeRtos::worker;return 1;}
inline void vTaskDelay(int n){std::this_thread::sleep_for(std::chrono::milliseconds(n));}
inline uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t){return 12288;}
