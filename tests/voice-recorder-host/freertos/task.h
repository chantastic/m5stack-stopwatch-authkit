#pragma once
#include <cstdint>
struct FakeTask;
using TaskHandle_t=FakeTask*;
int xTaskCreatePinnedToCore(void (*fn)(void*),const char*,uint32_t,void*,int,TaskHandle_t*,int);
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(int,uint32_t);
void vTaskDelay(uint32_t);
uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t);
