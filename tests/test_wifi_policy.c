#include "wifi_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    tracker_wifi_candidate_t p[4]={{true,-40,0},{true,-65,0},{false,-10,0},{true,-1000,0}};
    assert(tracker_wifi_choose(p,4,-1,100000)==0);
    p[0].tried_ms=95000;assert(tracker_wifi_choose(p,4,-1,100000)==1);
    p[1].tried_ms=95000;assert(tracker_wifi_choose(p,4,-1,100000)==3);
    assert(tracker_wifi_choose(p,4,0,100000)==-1);
    assert(tracker_wifi_choose(p,4,0,125000)==0);
    assert(tracker_wifi_choose(p,4,2,130000)==-1);
    assert(!tracker_wifi_fallback_due(false,false,100,60099));
    assert(tracker_wifi_fallback_due(false,false,100,60100));
    assert(!tracker_wifi_fallback_due(true,false,100,70000));
    assert(!tracker_wifi_fallback_due(false,true,100,70000));
    /* No-IP association uses the same timeout as an unavailable SSID. */
    assert(tracker_wifi_fallback_due(false,false,100,70000));
    assert(!tracker_wifi_hotspot_on(2,true,false,true,0,100,70000));
    assert(!tracker_wifi_hotspot_on(2,false,false,false,0,100,70000));
    assert(tracker_wifi_hotspot_on(1,true,true,false,0,0,70000));
    assert(tracker_wifi_hotspot_on(0,false,false,false,0,100,100));
    assert(!tracker_wifi_hotspot_on(0,true,false,false,0,100,60099));
    assert(tracker_wifi_hotspot_on(0,true,false,false,0,100,60100));
    assert(tracker_wifi_hotspot_on(0,true,true,true,100,0,60099));
    assert(!tracker_wifi_hotspot_on(0,true,true,true,100,0,60100));
    puts("WLAN policy: strongest profile, failover, cooldown, lock and 60-second no-IP timeout passed");
}
