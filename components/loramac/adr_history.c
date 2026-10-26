#include "adr_history.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static lorawan_adr_event_t events[ADR_HISTORY_CAPACITY];static uint32_t total;
void lorawan_adr_record(lorawan_adr_event_t event){event.uptime_ms=esp_timer_get_time()/1000;portENTER_CRITICAL(&mux);event.id=++total;events[(total-1)%ADR_HISTORY_CAPACITY]=event;portEXIT_CRITICAL(&mux);}
size_t lorawan_adr_history(lorawan_adr_event_t *out,size_t capacity,uint32_t *count){portENTER_CRITICAL(&mux);size_t n=total<ADR_HISTORY_CAPACITY?total:ADR_HISTORY_CAPACITY;if(n>capacity)n=capacity;for(size_t i=0;i<n;i++)out[i]=events[(total-1-i)%ADR_HISTORY_CAPACITY];if(count)*count=total;portEXIT_CRITICAL(&mux);return n;}
