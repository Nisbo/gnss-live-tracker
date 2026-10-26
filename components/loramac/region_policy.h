#pragma once
#include <stdint.h>
#include <stdbool.h>
/* Stable application IDs; independent of the vendor MAC enum. */
enum {TRACKER_REGION_EU868,TRACKER_REGION_US915,TRACKER_REGION_AU915,
 TRACKER_REGION_IN865,TRACKER_REGION_KR920,TRACKER_REGION_AS923_1,
 TRACKER_REGION_AS923_2,TRACKER_REGION_AS923_3,TRACKER_REGION_AS923_4,
 TRACKER_REGION_RU864,TRACKER_REGION_COUNT};
static inline bool tracker_region_valid(unsigned r){return r<TRACKER_REGION_COUNT;}
static inline bool tracker_region_as923(unsigned r){return r>=5&&r<=8;}
static inline bool tracker_region_fsb(unsigned r){return r==1||r==2;}
static inline bool tracker_region_dwell(unsigned r){return r==2||tracker_region_as923(r);}
static inline const char *tracker_region_name(unsigned r){
 static const char *const names[]={"EU868","US915","AU915","IN865","KR920","AS923-1","AS923-2","AS923-3","AS923-4","RU864"};
 return tracker_region_valid(r)?names[r]:"Unknown";
}
/* Starting rates: 0=125kHz, 1=500kHz; other widths are not selected. */
static inline int tracker_region_dr(unsigned r,unsigned sf,unsigned wide){
 if(!tracker_region_valid(r))return -1;
 if(r==1)return wide==1?(sf==8?4:-1):wide==0&&sf>=7&&sf<=10?10-(int)sf:-1;
 if(r==2)return wide==1?(sf==8?6:-1):wide==0&&sf>=7&&sf<=12?12-(int)sf:-1;
 if(wide==2)return -1;
 return wide==0&&sf>=7&&sf<=12?12-(int)sf:-1;
}
static inline bool tracker_region_rate(unsigned r,unsigned dr,uint8_t *sf,uint16_t *bw){
 if(!tracker_region_valid(r))return false;
 if(r==1){if(dr<=3){*sf=10-dr;*bw=125;return true;}if(dr==4){*sf=8;*bw=500;return true;}}
 else if(dr<=5){*sf=12-dr;*bw=125;return true;}
 else if(dr==6&&r!=4){*sf=r==2?8:7;*bw=r==2?500:250;return true;}
 if(tracker_region_fsb(r)&&dr>=8&&dr<=13){*sf=20-dr;*bw=500;return true;}
 return false;
}
static inline bool tracker_us915_mask(unsigned block,uint16_t mask[6]){
 if(block>8)return false;
 for(unsigned i=0;i<6;i++)mask[i]=0;
 if(!block){for(unsigned i=0;i<4;i++)mask[i]=0xffff;mask[4]=0xff;}
 else {unsigned b=block-1;mask[b/2]=(uint16_t)(0xffu<<((b%2)*8));mask[4]=(uint16_t)(1u<<b);}
 return true;
}
static inline unsigned tracker_region_payload_limit(unsigned r,unsigned dr){
 static const unsigned us[]={11,53,125,242,242};
 if(r==1)return dr<5?us[dr]:0;
 return tracker_region_valid(r)?(dr<=2?51:dr==3?115:dr<=6?242:0):0;
}
static inline unsigned tracker_region_payload_dwell(unsigned r,unsigned dr,bool dwell){
 static const unsigned limited[]={0,0,11,53,125,242,242};
 return tracker_region_dwell(r)&&dwell?(dr<7?limited[dr]:0):tracker_region_payload_limit(r,dr);
}
static inline unsigned tracker_packet_size(unsigned wanted,unsigned available){return wanted<=available?wanted:available>=11?11:available>=9?9:0;}

static inline bool tracker_region_frequency(unsigned r,uint32_t hz){
 static const uint32_t low[]={863000000,902000000,915000000,865000000,920900000,915000000,920000000,915000000,917000000,864000000};
 static const uint32_t high[]={870000000,928000000,928000000,867000000,923300000,928000000,923000000,921000000,920000000,870000000};
 return tracker_region_valid(r)&&hz>=low[r]&&hz<=high[r]&&(r!=4||(hz-920900000)%200000==0);
}
