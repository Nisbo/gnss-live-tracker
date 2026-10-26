#pragma once
#define pdPASS 1
int xTaskCreate(void (*fn)(void *),const char *,unsigned,void *,unsigned,void *);
void vTaskDelete(void *);
void vTaskDelay(unsigned);
