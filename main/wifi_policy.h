#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct { bool configured; int rssi; int64_t tried_ms; } tracker_wifi_candidate_t;
/* A functioning connection is retained by the caller. No BSSID is selected here. */
static inline int tracker_wifi_choose(const tracker_wifi_candidate_t *c,size_t count,int locked,int64_t now) {
    int best=-1,score=-10000;
    for(size_t i=0;i<count;i++) {
        if(!c[i].configured||(locked>=0&&(int)i!=locked))continue;
        if(c[i].tried_ms&&now-c[i].tried_ms<30000)continue;
        if(c[i].rssi>score){best=(int)i;score=c[i].rssi;}
    }
    return best;
}
static inline bool tracker_wifi_fallback_due(bool connected,bool hotspot,int64_t outage,int64_t now) {
    return !connected&&!hotspot&&outage>0&&now-outage>=60000;
}
