#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <string.h>
#include "tracker_timezones.h"
typedef enum { DISPLAY_ON=0, DISPLAY_AUTO=1, DISPLAY_OFF=2 } display_mode_t; /* OFF is legacy v1 only. */
typedef struct {uint8_t version,mode,brightness,reserved;uint32_t timeout_s;} display_config_v1_t;
#define DISPLAY_PAGE_COUNT 9
#define DISPLAY_START_LAST 255
/* Old records remain readable; OTA does not touch their NVS keys. */
typedef struct {uint8_t version,mode,brightness,boot_on;uint32_t timeout_s;uint8_t highlight,reserved[3];} display_config_v2_t;
typedef struct {uint8_t version,mode,brightness,boot_on;uint32_t timeout_s;uint8_t highlight,enabled_mask,order[8],reserved[2];} display_config_v3_t;
typedef struct {uint8_t version,mode,brightness,boot_on;uint32_t timeout_s;uint8_t highlight,start_page,order[9],rotate_s,saver_brightness,clock_zone,clock_seconds,reserved;uint16_t enabled_mask;} display_config_v4_t;
typedef struct {uint8_t version,mode,brightness,boot_on;uint32_t timeout_s;uint8_t highlight,start_page,order[9],rotate_s,saver_brightness,clock_zone,clock_seconds,language;uint16_t enabled_mask;char timezone[48];} display_config_t;
_Static_assert(sizeof(display_config_v2_t)==12,"v2 record");
_Static_assert(sizeof(display_config_v3_t)==20,"v3 record");
_Static_assert(sizeof(display_config_v4_t)==28,"v4 record");
_Static_assert(sizeof(display_config_t)==76,"v5 record");
_Static_assert(offsetof(display_config_t,language)==offsetof(display_config_v4_t,reserved),"language reuses legacy reserved byte");
/* mode: always on, timeout off, timeout clock. Numeric clock_zone is retained for v4 migration only. */
static inline bool display_timeout_valid(unsigned s){return s==15||s==30||s==60||s==120||s==300||s==600;}
static inline bool display_rotation_valid(unsigned s){return s==0||s==5||s==10||s==15||s==30||s==60;}
static inline bool display_order_valid(const uint8_t order[DISPLAY_PAGE_COUNT]){unsigned mask=0;for(unsigned i=0;i<DISPLAY_PAGE_COUNT;i++){if(order[i]>=DISPLAY_PAGE_COUNT||(mask&(1u<<order[i])))return false;mask|=1u<<order[i];}return mask==511;}
static inline bool display_config_valid(display_config_t c){return c.version==5&&c.mode<=2&&c.brightness>=1&&c.brightness<=100&&c.boot_on<=1&&display_timeout_valid(c.timeout_s)&&c.highlight<=7&&c.enabled_mask&&!(c.enabled_mask&~511u)&&display_order_valid(c.order)&&(c.start_page<9||c.start_page==255)&&display_rotation_valid(c.rotate_s)&&c.saver_brightness>=1&&c.saver_brightness<=100&&c.clock_zone<=4&&c.clock_seconds<=1&&c.language<=1&&memchr(c.timezone,0,sizeof(c.timezone))&&tracker_timezone_find(c.timezone)>=0;}
static inline bool display_config_migrate_v4(display_config_v4_t old,display_config_t *out){
 if(old.version!=4||old.clock_zone>4)return false;
 static const char *const legacy[]={"UTC","Europe/Berlin","Europe/London","America/New_York","Asia/Tokyo"};
 memset(out,0,sizeof(*out));memcpy(out,&old,sizeof(old));out->version=5;out->language=1;strcpy(out->timezone,legacy[old.clock_zone]);return display_config_valid(*out);
}
static inline bool display_config_migrate_v3(display_config_v3_t old,display_config_t *out){
 unsigned mask=0;for(unsigned i=0;i<8;i++){if(old.order[i]>=8||(mask&(1u<<old.order[i])))return false;mask|=1u<<old.order[i];}
 if(old.version!=3||old.mode>1||old.brightness<1||old.brightness>100||old.boot_on>1||!display_timeout_valid(old.timeout_s)||old.highlight>7||!old.enabled_mask)return false;
 *out=(display_config_t){.version=5,.language=1,.mode=old.mode,.brightness=old.brightness,.boot_on=old.boot_on,.timeout_s=old.timeout_s,.highlight=old.highlight,.start_page=0,.rotate_s=0,.saver_brightness=15,.clock_zone=1,.clock_seconds=1,.enabled_mask=old.enabled_mask,.timezone="Europe/Berlin"};for(unsigned i=0;i<8;i++)out->order[i]=old.order[i];out->order[8]=8;return true;
}
static inline bool display_config_migrate_v2(display_config_v2_t old,display_config_t *out){
 if(old.version!=2)return false;
 return display_config_migrate_v3((display_config_v3_t){3,old.mode,old.brightness,old.boot_on,old.timeout_s,old.highlight,255,{0,1,2,3,4,5,6,7},{0}},out);
}
static inline bool display_config_migrate(display_config_v1_t old,display_config_t *out){
 if(old.version!=1||old.mode>DISPLAY_OFF||old.brightness<1||old.brightness>100||!display_timeout_valid(old.timeout_s))return false;
 return display_config_migrate_v2((display_config_v2_t){2,old.mode==DISPLAY_ON?DISPLAY_ON:DISPLAY_AUTO,old.brightness,old.mode!=DISPLAY_OFF,old.timeout_s,3,{0}},out);
}
static inline unsigned display_next_page(display_config_t c,unsigned current){
 unsigned first=0,position=0;bool found=false;for(unsigned i=0;i<DISPLAY_PAGE_COUNT;i++){if(c.enabled_mask&(1u<<c.order[i])){if(!found){first=c.order[i];found=true;}}if(c.order[i]==current)position=i;}
 if(current>=DISPLAY_PAGE_COUNT||!(c.enabled_mask&(1u<<current)))return first;
 for(unsigned step=1;step<=DISPLAY_PAGE_COUNT;step++){unsigned page=c.order[(position+step)%DISPLAY_PAGE_COUNT];if(c.enabled_mask&(1u<<page))return page;}return 0;
}
/* Runtime only: automatic pages never overwrite the remembered manual page. */
typedef struct {unsigned page,return_page;bool saver;int64_t deadline,next_rotation;} display_runtime_t;
static inline void display_runtime_start(display_runtime_t *r,display_config_t c,unsigned last,int64_t now){*r=(display_runtime_t){.page=c.start_page==255?(last<9?last:0):c.start_page,.deadline=c.boot_on?now+(int64_t)c.timeout_s*1000:0,.next_rotation=now+(int64_t)c.rotate_s*1000};}
static inline void display_runtime_interact(display_runtime_t *r,display_config_t c,int64_t now){if(r->saver){r->page=r->return_page;r->saver=false;}r->deadline=now+(int64_t)c.timeout_s*1000;r->next_rotation=now+(int64_t)(c.rotate_s>30?c.rotate_s:30)*1000;}
static inline bool display_runtime_tick(display_runtime_t *r,display_config_t c,int64_t now){
 bool changed=false;
 if(c.mode==2&&now>=r->deadline&&!r->saver){r->return_page=r->page;r->page=8;r->saver=true;changed=true;}
 bool on=c.mode==0||c.mode==2||now<r->deadline;
 if(on&&!r->saver&&c.rotate_s&&now>=r->next_rotation){unsigned next=display_next_page(c,r->page);changed=next!=r->page;r->page=next;r->next_rotation=now+(int64_t)c.rotate_s*1000;}
 return changed;
}
static inline bool display_visible(display_mode_t mode,int64_t now,int64_t deadline){return mode==DISPLAY_ON||(deadline>now&&deadline>0);}
typedef struct {int64_t since,released;bool down,fired,pending,pending_next,second;} display_button_t;
typedef enum { DISPLAY_BUTTON_NONE,DISPLAY_BUTTON_WAKE,DISPLAY_BUTTON_HOTSPOT,DISPLAY_BUTTON_NEXT,DISPLAY_BUTTON_HOME } display_button_event_t;
static inline display_button_event_t display_button_update(display_button_t *b,bool down,int64_t now,bool screen_on){
 display_button_event_t event=DISPLAY_BUTTON_NONE;
 if(down){
  if(!b->down){b->second=b->pending&&now-b->released<=300;if(b->pending&&!b->second){if(b->pending_next)event=DISPLAY_BUTTON_NEXT;b->pending=false;}b->since=now;b->down=true;b->fired=false;}
  if(!b->fired&&now-b->since>=3000){b->fired=true;b->pending=false;return DISPLAY_BUTTON_HOTSPOT;}
 }else if(b->down){
  int64_t held=now-b->since;bool fired=b->fired,second=b->second;b->down=false;b->fired=false;b->second=false;
  if(fired){b->pending=false;return DISPLAY_BUTTON_NONE;}
  if(held>=3000){b->pending=false;return DISPLAY_BUTTON_HOTSPOT;}
  if(held<40)return event;
  if(second&&held<=500){b->pending=false;return DISPLAY_BUTTON_HOME;}
  b->pending=true;b->pending_next=screen_on;b->released=now;return DISPLAY_BUTTON_WAKE;
 }else if(b->pending&&now-b->released>=300){b->pending=false;if(b->pending_next)event=DISPLAY_BUTTON_NEXT;}
 return event;
}
