/* Execute the production regional MAC with a recording radio, without RF. */
#include "Region.h"
#include "RegionUS915.h"
#include "region_policy.h"
#include "sx1262_calibration.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned frequency,tx_bw,tx_sf,rx_bw,rx_sf,checks;
static int8_t radio_power;
static RadioState_t idle(void){return RF_IDLE;}
static void channel(uint32_t hz){frequency=hz;checks++;}
static uint32_t wakeup(void){return 3;}
static bool valid_frequency(uint32_t hz){return hz>=863000000&&hz<=928000000;}
static void maximum(RadioModems_t m,uint8_t n){(void)m;(void)n;}
static uint32_t airtime(RadioModems_t m,uint8_t n){(void)m;return n*2;}
static void rx(RadioModems_t m,uint32_t bw,uint32_t sf,uint8_t cr,uint32_t afc,uint16_t preamble,uint16_t timeout,bool fixed,uint8_t len,bool crc,bool hop,uint8_t period,bool iq,bool continuous){(void)m;(void)cr;(void)afc;(void)preamble;(void)timeout;(void)fixed;(void)len;(void)crc;(void)hop;(void)period;(void)iq;(void)continuous;rx_bw=bw;rx_sf=sf;}
static void tx(RadioModems_t m,int8_t power,uint32_t fdev,uint32_t bw,uint32_t sf,uint8_t cr,uint16_t preamble,bool fixed,bool crc,bool hop,uint8_t period,bool iq,uint32_t timeout){(void)m;(void)fdev;(void)cr;(void)preamble;(void)fixed;(void)crc;(void)hop;(void)period;(void)iq;(void)timeout;tx_bw=bw;tx_sf=sf;radio_power=power;}
static bool channel_free=true;static unsigned lbt_checks;
static bool sense(RadioModems_t m,uint32_t hz,int16_t threshold,uint32_t duration){(void)m;(void)hz;(void)threshold;assert(duration>0);lbt_checks++;return channel_free;}
const struct Radio_s Radio={.IsChannelFree=sense,.GetStatus=idle,.GetWakeupTime=wakeup,.CheckRfFrequency=valid_frequency,.SetChannel=channel,.SetRxConfig=rx,.SetTxConfig=tx,.SetMaxPayloadLength=maximum,.TimeOnAir=airtime};
TimerTime_t TimerGetElapsedTime(TimerTime_t past){return 1000000-past;}
int32_t randr(int32_t a,int32_t b){(void)b;return a;}
static PhyParam_t phy(PhyAttribute_t a,int8_t dr){GetPhyParams_t p={.Attribute=a,.Datarate=dr};return RegionUS915GetPhyParam(&p);}
#ifndef TRACKER_RADIO_STUB_ONLY
int main(void){
 RegionUS915InitDefaults(INIT_TYPE_INIT);
 ChannelParams_t *channels=phy(PHY_CHANNELS,0).Channels;
 for(unsigned i=0;i<64;i++){assert(channels[i].Frequency==902300000+i*200000);assert(channels[i].DrRange.Fields.Min==0&&channels[i].DrRange.Fields.Max==3);}
 for(unsigned i=64;i<72;i++)assert(channels[i].Frequency==903000000+(i-64)*1600000);
 assert(phy(PHY_DEF_RX2_FREQUENCY,0).Value==923300000&&phy(PHY_DEF_RX2_DR,0).Value==8);
 for(unsigned block=0;block<=8;block++){
  uint16_t mask[6];assert(tracker_us915_mask(block,mask));ChanMaskSetParams_t m={.ChannelsMaskIn=mask,.ChannelsMaskType=CHANNELS_DEFAULT_MASK};assert(RegionUS915ChanMaskSet(&m));m.ChannelsMaskType=CHANNELS_MASK;assert(RegionUS915ChanMaskSet(&m));
  RegionUS915InitDefaults(INIT_TYPE_RESTORE);assert(!memcmp(mask,phy(PHY_CHANNELS_MASK,0).ChannelsMask,sizeof(mask)));
  for(unsigned trial=1;trial<=20;trial++){
   AlternateDrParams_t alt={.NbTrials=trial};int8_t dr=RegionUS915AlternateDr(&alt);assert(dr==(trial%2?4:0));
   NextChanParams_t next={.Datarate=dr,.Joined=false};uint8_t c;TimerTime_t delay,aggregated;assert(RegionUS915NextChannel(&next,&c,&delay,&aggregated));assert(mask[c/16]&(1u<<(c%16)));assert(dr==4?c>=64:c<64);if(block)assert(dr==4?c==63+block:c>=(block-1)*8&&c<block*8);
  }
 }
 uint16_t mask[6];assert(tracker_us915_mask(2,mask));assert(mask[0]==0xff00&&mask[4]==2);assert(!tracker_us915_mask(9,mask));tracker_us915_mask(2,mask);
 ChanMaskSetParams_t m={mask,CHANNELS_DEFAULT_MASK};assert(RegionUS915ChanMaskSet(&m));m.ChannelsMaskType=CHANNELS_MASK;assert(RegionUS915ChanMaskSet(&m));
 for(int dr=0;dr<=4;dr++){
  int8_t sf=dr==4?8:10-dr;unsigned bw=dr==4?2:0;uint8_t helper_sf;uint16_t helper_bw;assert(tracker_region_rate(1,dr,&helper_sf,&helper_bw)&&helper_sf==sf&&helper_bw==(dr==4?500:125));assert(tracker_region_dr(1,sf,dr==4)==dr);
  assert(phy(PHY_MAX_PAYLOAD,dr).Value==tracker_region_payload_limit(1,dr));
  TxConfigParams_t t={.Channel=dr==4?65:8,.Datarate=dr,.TxPower=0,.MaxEirp=30,.AntennaGain=0,.PktLen=20};int8_t power;TimerTime_t air;assert(RegionUS915TxConfig(&t,&power,&air));assert(frequency==(dr==4?904600000:903900000)&&tx_sf==sf&&tx_bw==bw&&air==40&&radio_power<=30);
  for(int offset=0;offset<=3;offset++){int expect=10+dr-offset;if(expect<8)expect=8;if(expect>13)expect=13;assert(RegionUS915ApplyDrOffset(0,dr,offset)==expect);}
 }
 for(unsigned channel=0;channel<72;channel++)for(int dr=8;dr<=13;dr++){
  RxConfigParams_t r={.Channel=channel,.Datarate=dr,.Bandwidth=2,.RxSlot=RX_SLOT_WIN_1};int8_t out;assert(RegionUS915RxConfig(&r,&out));assert(frequency==923300000+(channel%8)*600000&&rx_sf==20-dr&&rx_bw==2&&out==dr);
 }
 for(unsigned ack=0;ack<260;ack++){
  AdrNextParams_t a={.AdrEnabled=true,.Datarate=1,.TxPower=5,.AdrAckCounter=ack,.UpdateChanMask=true};int8_t dr,power;uint32_t counter;RegionUS915AdrNext(&a,&dr,&power,&counter);if(ack>=64)assert(power==0);assert(dr==((ack>=96&&ack%32==1)?0:1));assert(!memcmp(mask,phy(PHY_CHANNELS_MASK,0).ChannelsMask,sizeof(mask)));
 }
 uint8_t bytes[2];assert(sx1262_image_band(868100000,bytes)==1&&bytes[0]==0xd7&&bytes[1]==0xd8);for(unsigned hz=902300000;hz<=927500000;hz+=200000)assert(sx1262_image_band(hz,bytes)&&bytes[0]==0xe1&&bytes[1]==0xe9);assert(!sx1262_image_band(433000000,bytes));
 for(int sf=7;sf<=12;sf++){assert(tracker_region_dr(0,sf,false)==12-sf);assert(tracker_region_dr(1,sf,false)==(sf<=10?10-sf:-1));}assert(tracker_region_dr(0,8,true)<0);assert(tracker_packet_size(38,53)==38&&tracker_packet_size(38,11)==11&&tracker_packet_size(24,9)==9&&tracker_packet_size(23,8)==0);
 printf("US915 production regional MAC: 72 channels, all FSBs, join cycling, DR0–4, RX1/RX2, ADR recovery, payload limits and RF calibration PASS (%u recorded radio configurations; no RF)\n",checks);
}

#endif
