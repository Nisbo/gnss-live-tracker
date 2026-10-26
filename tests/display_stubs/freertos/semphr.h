#pragma once
#include <stdbool.h>
typedef void *SemaphoreHandle_t;
#define pdTRUE 1
SemaphoreHandle_t xSemaphoreCreateMutex(void);
bool xSemaphoreTake(SemaphoreHandle_t,unsigned);
void xSemaphoreGive(SemaphoreHandle_t);
