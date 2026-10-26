#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* Wire codes: 0 unknown, 1 unavailable, 2 reachable. */
typedef struct {uint8_t version,automatic,position,events;} internet_config_t;
static inline bool internet_config_valid(internet_config_t c){return c.version==1&&c.automatic<=1&&c.position<=1&&c.events<=1&&(c.automatic||(!c.position&&!c.events));}
typedef struct {uint8_t state,candidate,streak,reported;int64_t changed_ms,last_event_ms,candidate_ms;} internet_policy_t;
static inline void internet_observe(internet_policy_t *p,bool online,int64_t now){
 uint8_t state=online?2:1;
 if(!p->state){p->state=p->candidate=p->reported=state;p->streak=1;p->changed_ms=now;return;}
 if(state==p->state){p->candidate=state;p->streak=0;p->candidate_ms=0;return;}
 if(p->candidate!=state){p->candidate=state;p->streak=1;p->candidate_ms=now;return;}
 if(now-p->candidate_ms>=60000){p->state=state;p->streak=2;p->changed_ms=now;p->candidate_ms=0;}
}
static inline int64_t internet_check_delay(const internet_policy_t *p,int64_t now){
 if(p->candidate!=p->state){int64_t remaining=60000-(now-p->candidate_ms);return remaining>0?remaining:0;}
 return 300000;
}
static inline bool internet_event_due(const internet_policy_t *p,bool enabled,int64_t now){return enabled&&p->state&&p->state!=p->reported&&(!p->last_event_ms||now-p->last_event_ms>=300000);}
/* Standard profiles reserve the high flag bits; lengths and earlier fields stay unchanged. */
static inline void internet_position(uint8_t *packet,size_t n,unsigned state){if(packet&&n>=23&&packet[0]>=3&&packet[0]<=5)packet[1]=(packet[1]&63)|((state+1)<<6);}
static inline void internet_event_packet(uint8_t out[8],unsigned state,bool wifi,uint32_t utc,unsigned failures){out[0]=1;out[1]=state;out[2]=wifi?1:0;out[3]=utc>>24;out[4]=utc>>16;out[5]=utc>>8;out[6]=utc;out[7]=failures>255?255:failures;}
