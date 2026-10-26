#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Wire codes: 0 unknown, 1 unavailable, 2 reachable. */
typedef struct {uint8_t version,automatic,position,events;} internet_config_t;
static inline bool internet_config_valid(internet_config_t c){return c.version==1&&c.automatic<=1&&c.position<=1&&c.events<=1&&(c.automatic||(!c.position&&!c.events));}
typedef struct {uint8_t state,candidate,streak,reported;int64_t changed_ms,last_event_ms;} internet_policy_t;
static inline void internet_observe(internet_policy_t *p,bool online,int64_t now){
 uint8_t state=online?2:1;if(p->candidate!=state){p->candidate=state;p->streak=0;}if(p->streak<3)p->streak++;
 if(p->state!=state&&p->streak>=(online?(p->state?2:1):3)){bool initial=!p->state;p->state=state;p->changed_ms=now;if(initial)p->reported=state;}
}
static inline bool internet_event_due(const internet_policy_t *p,bool enabled,int64_t now){return enabled&&p->state&&p->state!=p->reported&&(!p->last_event_ms||now-p->last_event_ms>=300000);}
/* Standard profiles reserve the high flag bits; lengths and earlier fields stay unchanged. */
static inline void internet_position(uint8_t *packet,size_t n,unsigned state){if(packet&&n>=23&&packet[0]>=3&&packet[0]<=5)packet[1]=(packet[1]&63)|((state+1)<<6);}
static inline void internet_event_packet(uint8_t out[8],unsigned state,bool wifi,uint32_t utc,unsigned failures){out[0]=1;out[1]=state;out[2]=wifi?1:0;out[3]=utc>>24;out[4]=utc>>16;out[5]=utc>>8;out[6]=utc;out[7]=failures>255?255:failures;}
