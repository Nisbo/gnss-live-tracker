#pragma once
#include <stdint.h>
#include <stddef.h>
#define ADR_HISTORY_CAPACITY 8
typedef struct {uint64_t uptime_ms;uint32_t id;uint8_t source,status,enabled,commands;int8_t before_dr,before_power,requested_dr,requested_power,after_dr,after_power,before_dbm,after_dbm;uint8_t before_repetitions,requested_repetitions,after_repetitions,reserved;} lorawan_adr_event_t;
void lorawan_adr_record(lorawan_adr_event_t event);
size_t lorawan_adr_history(lorawan_adr_event_t *out,size_t capacity,uint32_t *total);
