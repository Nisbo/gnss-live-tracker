#include "tracker_timezones.h"
#include <string.h>
typedef struct {uint32_t minute;int16_t offset;} tz_transition_t;
typedef struct {const char *name;uint16_t first,count;} tz_zone_t;
#include "timezone_data.inc"
int tracker_timezone_find(const char *name){
 if(!name)return -1;
 int lo=0,hi=sizeof(zones)/sizeof(zones[0])-1;
 while(lo<=hi){int mid=(lo+hi)/2,c=strcmp(name,zones[mid].name);if(!c)return mid;if(c<0)hi=mid-1;else lo=mid+1;}return -1;
}
bool tracker_timezone_offset(const char *name,time_t epoch,int *minutes){
 if(!minutes)return false;
 int id=tracker_timezone_find(name);
 if(id<0||(int64_t)epoch<1577836800LL||(int64_t)epoch>=4133980800LL)return false;
 const tz_zone_t *zone=&zones[id];uint32_t minute=((int64_t)epoch-1577836800LL)/60;unsigned lo=0,hi=zone->count;
 while(lo+1<hi){unsigned mid=(lo+hi)/2;if(transitions[zone->first+mid].minute<=minute)lo=mid;else hi=mid;}
 *minutes=transitions[zone->first+lo].offset;return true;
}
