#include <assert.h>
#include "display_policy.h"
#include "display_language.h"
int main(void){
 int minutes;
 /* Translation reuses the old zero-initialized reserved byte in the unchanged v5 record. */
 assert(sizeof(display_config_t)==76);assert(offsetof(display_config_t,language)==23);
 for(unsigned id=0;id<DISPLAY_TEXT_COUNT;id++)for(unsigned language=0;language<2;language++){
  const char *label=display_text(language,(display_text_id_t)id);assert(strlen(label)>0&&strlen(label)<=22);
  for(const unsigned char *p=(const unsigned char *)label;*p;p++)assert(*p>=32&&*p<127);
  const char *de=display_text(0,(display_text_id_t)id),*en=display_text(1,(display_text_id_t)id);
  while((de=strchr(de,'%'))){en=strchr(en,'%');assert(en);do{assert(*de==*en);de++;en++;}while(*de&&strchr("0123456789.l",*de));assert(*de==*en);de++;en++;}assert(!strchr(en,'%'));
 }
 assert(!strcmp(display_text(0,DISPLAY_TEXT_NETWORK),"NETZWERK"));assert(!strcmp(display_text(1,DISPLAY_TEXT_NETWORK),"NETWORK"));assert(!strcmp(display_weekday(0,0),"So")&&!strcmp(display_weekday(1,0),"Sun"));assert(!strcmp(display_month(9),"Oct"));

 assert(display_timeout_valid(15)&&display_timeout_valid(600)&&!display_timeout_valid(0)&&!display_timeout_valid(59));assert(display_visible(DISPLAY_ON,999999,0));assert(display_visible(DISPLAY_AUTO,1000,2000));assert(!display_visible(DISPLAY_AUTO,2000,2000));
 display_config_t c;for(unsigned m=0;m<3;m++){assert(display_config_migrate((display_config_v1_t){1,m,35,0,30},&c));assert(display_config_valid(c));assert(c.language==1);assert(c.mode==(m==DISPLAY_ON?DISPLAY_ON:DISPLAY_AUTO));assert(c.boot_on==(m!=DISPLAY_OFF));assert(c.brightness==35&&c.timeout_s==30&&c.highlight==3);}assert(!display_config_migrate((display_config_v1_t){1,3,100,0,60},&c));c.highlight=8;assert(!display_config_valid(c));
 assert(display_config_migrate_v2((display_config_v2_t){2,DISPLAY_AUTO,42,0,15,0,{0}},&c));assert(c.enabled_mask==255&&c.highlight==0&&c.boot_on==0);for(unsigned i=0;i<8;i++)assert(display_next_page(c,i)==(i+1)%8);
 uint8_t order[8]={5,4,6,7,0,1,2,3};for(unsigned i=0;i<8;i++)c.order[i]=order[i];c.enabled_mask=(1u<<5)|(1u<<7);assert(display_config_valid(c));assert(display_next_page(c,0)==5);assert(display_next_page(c,5)==7);assert(display_next_page(c,7)==5);c.enabled_mask=0;assert(!display_config_valid(c));c.enabled_mask=255;c.order[0]=4;assert(!display_config_valid(c));
 /* v3 migration preserves subset/order; the new clock stays opt-in. */
 display_config_v3_t old3={3,1,42,0,60,3,48,{5,4,0,1,2,3,6,7},{0}};assert(display_config_migrate_v3(old3,&c));assert(c.enabled_mask==48&&c.order[8]==8&&c.rotate_s==0&&c.start_page==0&&c.saver_brightness==15);assert(display_config_valid(c));
 c.language=1;assert(display_config_valid(c));c.language=2;assert(!display_config_valid(c));c.language=0;
 display_config_v4_t old4;memcpy(&old4,&c,sizeof(old4));old4.version=4;old4.clock_zone=3;old4.reserved=255;assert(display_config_migrate_v4(old4,&c));assert(c.language==1);assert(!strcmp(c.timezone,"America/New_York")&&c.enabled_mask==48&&c.order[0]==5);old4.clock_zone=5;assert(!display_config_migrate_v4(old4,&c));
 c.enabled_mask=(1u<<4)|(1u<<8);c.rotate_s=5;c.mode=0;c.start_page=255;display_runtime_t r;display_runtime_start(&r,c,4,0);assert(r.page==4);assert(!display_runtime_tick(&r,c,4999));assert(display_runtime_tick(&r,c,5000)&&r.page==8);assert(display_runtime_tick(&r,c,10000)&&r.page==4);
 display_runtime_interact(&r,c,11000);assert(!display_runtime_tick(&r,c,40999));assert(display_runtime_tick(&r,c,41000)&&r.page==8);
 c.mode=2;c.timeout_s=15;c.boot_on=1;display_runtime_start(&r,c,4,0);display_runtime_tick(&r,c,15000);assert(r.saver&&r.page==8);unsigned previous=r.return_page;assert(!display_runtime_tick(&r,c,90000));display_runtime_interact(&r,c,90001);assert(!r.saver&&r.page==previous);assert(!display_runtime_tick(&r,c,90002));
 c.boot_on=0;display_runtime_start(&r,c,4,0);display_runtime_tick(&r,c,0);assert(r.saver);c.mode=1;display_runtime_start(&r,c,4,0);assert(!display_runtime_tick(&r,c,60000)&&r.page==4);
 c.rotate_s=4;assert(!display_config_valid(c));c.rotate_s=0;c.start_page=9;assert(!display_config_valid(c));c.start_page=8;assert(display_config_valid(c));c.enabled_mask=512;assert(!display_config_valid(c));
 /* DST transition at 01:00 UTC in Europe and 07/06 UTC in New York. */
 assert(tracker_timezone_offset("Europe/Berlin",1792889999,&minutes)&&minutes==120);assert(tracker_timezone_offset("Europe/Berlin",1792890000,&minutes)&&minutes==60);assert(tracker_timezone_offset("Europe/London",1792890000,&minutes)&&minutes==0);assert(tracker_timezone_offset("UTC",1792890000,&minutes)&&minutes==0);assert(tracker_timezone_offset("Asia/Tokyo",1792890000,&minutes)&&minutes==540);
 display_button_t b={0};assert(display_button_update(&b,true,0,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,39,true)==DISPLAY_BUTTON_NONE);
 assert(display_button_update(&b,true,100,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,150,true)==DISPLAY_BUTTON_WAKE);assert(display_button_update(&b,false,449,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,450,true)==DISPLAY_BUTTON_NEXT);assert(display_button_update(&b,false,500,true)==DISPLAY_BUTTON_NONE);
 assert(display_button_update(&b,true,1000,false)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,1100,false)==DISPLAY_BUTTON_WAKE);assert(display_button_update(&b,false,1400,true)==DISPLAY_BUTTON_NONE);
 assert(display_button_update(&b,true,2000,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,2100,true)==DISPLAY_BUTTON_WAKE);assert(display_button_update(&b,true,2250,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,2350,true)==DISPLAY_BUTTON_HOME);assert(display_button_update(&b,false,2800,true)==DISPLAY_BUTTON_NONE);
 assert(display_button_update(&b,true,3000,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,true,5999,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,true,6000,true)==DISPLAY_BUTTON_HOTSPOT);assert(display_button_update(&b,true,8000,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,8100,true)==DISPLAY_BUTTON_NONE);
 assert(display_button_update(&b,true,9000,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,12000,true)==DISPLAY_BUTTON_HOTSPOT);assert(display_button_update(&b,false,13000,true)==DISPLAY_BUTTON_NONE);
 /* A second press held for hotspot must cancel pending page advance. */
 assert(display_button_update(&b,true,14000,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,14100,true)==DISPLAY_BUTTON_WAKE);assert(display_button_update(&b,true,14200,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,true,17200,true)==DISPLAY_BUTTON_HOTSPOT);assert(display_button_update(&b,false,17300,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,18000,true)==DISPLAY_BUTTON_NONE);
 /* Bounce on the second press preserves the first click. */
 assert(display_button_update(&b,true,19000,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,19100,true)==DISPLAY_BUTTON_WAKE);assert(display_button_update(&b,true,19200,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,19205,true)==DISPLAY_BUTTON_NONE);assert(display_button_update(&b,false,19400,true)==DISPLAY_BUTTON_NEXT);return 0;
}
