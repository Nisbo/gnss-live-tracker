#include "timer.h"

#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "lmn_timer";

TimerEvent_t *TimerListHead = NULL;

typedef struct {
    TimerEvent_t *obj;
    esp_timer_handle_t h;
} timer_map_t;

// LoRaMac uses only a handful of timers; keep a small fixed map.
static timer_map_t s_map[16];

static esp_timer_handle_t find_handle(TimerEvent_t *obj)
{
    for (size_t i = 0; i < sizeof(s_map)/sizeof(s_map[0]); i++) {
        if (s_map[i].obj == obj) return s_map[i].h;
    }
    return NULL;
}

static esp_timer_handle_t ensure_handle(TimerEvent_t *obj)
{
    esp_timer_handle_t h = find_handle(obj);
    if (h) return h;

    for (size_t i = 0; i < sizeof(s_map)/sizeof(s_map[0]); i++) {
        if (s_map[i].obj == NULL) {
            esp_timer_create_args_t args = {
                .callback = (esp_timer_cb_t)obj->Callback,
                .arg = NULL,
                .dispatch_method = ESP_TIMER_TASK,
                .name = "lmn",
                .skip_unhandled_events = true,
            };
            if (esp_timer_create(&args, &h) != ESP_OK) {
                ESP_LOGE(TAG, "esp_timer_create failed");
                return NULL;
            }
            s_map[i].obj = obj;
            s_map[i].h = h;
            return h;
        }
    }
    ESP_LOGE(TAG, "timer map full");
    return NULL;
}

void TimerSetSysTime(TimerSysTime_t sysTime)
{
    (void)sysTime;
}

TimerSysTime_t TimerGetSysTime(void)
{
    TimerSysTime_t t = {0};
    // Best effort: use uptime
    int64_t us = esp_timer_get_time();
    t.Seconds = (uint32_t)(us / 1000000LL);
    t.SubSeconds = (int16_t)((us / 1000LL) % 1000LL);
    return t;
}

TimerSysTime_t SysTimeGetMcuTime(void)
{
    return TimerGetSysTime();
}

uint32_t SysTimeToMs(TimerSysTime_t sysTime)
{
    return (uint32_t)(sysTime.Seconds * 1000UL + (uint32_t)sysTime.SubSeconds);
}

TimerSysTime_t SysTimeFromMs(uint32_t timeMs)
{
    TimerSysTime_t t = {0};
    t.Seconds = timeMs / 1000UL;
    t.SubSeconds = (int16_t)(timeMs % 1000UL);
    return t;
}

void TimerInit(TimerEvent_t *obj, void (*callback)(void))
{
    if (!obj) return;
    memset(obj, 0, sizeof(*obj));
    obj->Callback = callback;
    obj->ReloadValue = 0;
    obj->Timestamp = 0;
    obj->IsRunning = false;
    obj->Next = NULL;
    (void)ensure_handle(obj);
}

void TimerIrqHandler(void)
{
    // Not used with esp_timer based implementation.
}

void TimerStart(TimerEvent_t *obj)
{
    if (!obj || !obj->Callback) return;
    esp_timer_handle_t h = ensure_handle(obj);
    if (!h) return;

    esp_timer_stop(h);
    obj->IsRunning = true;
    obj->Timestamp = (uint32_t)TimerGetCurrentTime() + obj->ReloadValue;
    esp_timer_start_once(h, (uint64_t)obj->ReloadValue * 1000ULL);
}

void TimerStop(TimerEvent_t *obj)
{
    if (!obj) return;
    esp_timer_handle_t h = find_handle(obj);
    if (h) esp_timer_stop(h);
    obj->IsRunning = false;
}

void TimerReset(TimerEvent_t *obj)
{
    if (!obj) return;
    TimerStart(obj);
}

void TimerSetValue(TimerEvent_t *obj, uint32_t value)
{
    if (!obj) return;
    obj->ReloadValue = value;
}

TimerTime_t TimerGetCurrentTime(void)
{
    // Return ms
    return (TimerTime_t)(esp_timer_get_time() / 1000ULL);
}

TimerTime_t TimerGetElapsedTime(TimerTime_t savedTime)
{
    TimerTime_t now = TimerGetCurrentTime();
    return (now >= savedTime) ? (now - savedTime) : 0;
}

TimerTime_t TimerTempCompensation(TimerTime_t period, float temperature)
{
    (void)temperature;
    return period;
}

void TimerLowPowerHandler(void)
{
    // Not implemented (ESP-IDF power management handled elsewhere)
}
