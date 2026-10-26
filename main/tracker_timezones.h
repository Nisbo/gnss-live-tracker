#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
/* Fixed flash catalog; no heap allocation, network requests or global TZ mutation. */
int tracker_timezone_find(const char *name);
bool tracker_timezone_offset(const char *name,time_t epoch,int *minutes);
