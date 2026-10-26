#pragma once
#include <stdbool.h>
#include <stdint.h>
static inline bool tracker_radio_interval_valid(unsigned s){
 switch(s){case 300:case 600:case 900:case 1200:case 1500:case 1800:case 2700:case 3600:return true;default:return false;}
}
/* Recompute from the last position, rather than postponing by a full interval
   whenever Save is pressed. Startup and first-fix reports are handled separately. */
static inline int64_t tracker_radio_position_due(int64_t last_us,uint16_t seconds,int64_t old_due){return last_us>=0?last_us+(int64_t)seconds*1000000LL:old_due;}
