/* Real regional modules with a recording radio: no GPIO, USB or RF access. */
#define TRACKER_RADIO_STUB_ONLY
#include "test_us915.c"
#include "RegionAS923.h"
#include "RegionAU915.h"
#include "radio_settings.h"
static const LoRaMacRegion_t regions[]={LORAMAC_REGION_EU868,LORAMAC_REGION_US915,LORAMAC_REGION_AU915,LORAMAC_REGION_IN865,LORAMAC_REGION_KR920,LORAMAC_REGION_AS923,LORAMAC_REGION_AS923,LORAMAC_REGION_AS923,LORAMAC_REGION_AS923,LORAMAC_REGION_RU864};
static PhyParam_t get(LoRaMacRegion_t r,PhyAttribute_t a,int dr,int dwell){GetPhyParams_t p={.Attribute=a,.Datarate=dr,.UplinkDwellTime=dwell};return RegionGetPhyParam(r,&p);}
void memcpy1(uint8_t *d,const uint8_t *s,uint16_t n){while(n--)*d++=*s++;}
int8_t defaultDrForNoAdr=0;
int main(void){
 const uint32_t first[]={868100000,902300000,915200000,865062500,922100000,923200000,921400000,916600000,917300000,868900000};
 const uint32_t rx2[]={869525000,923300000,923300000,866550000,921900000,923200000,921400000,916600000,917300000,869100000};
 const unsigned rx2dr[]={0,8,8,2,0,2,2,2,2,0};unsigned rates=0;
 for(unsigned id=0;id<TRACKER_REGION_COUNT;id++){
  LoRaMacRegion_t r=regions[id];if(tracker_region_as923(id))assert(RegionAS923SetGroup(id-4));RegionInitDefaults(r,INIT_TYPE_INIT);
  assert(get(r,PHY_CHANNELS,0,0).Channels[0].Frequency==first[id]);assert(get(r,PHY_DEF_RX2_FREQUENCY,0,0).Value==rx2[id]&&get(r,PHY_DEF_RX2_DR,0,0).Value==rx2dr[id]);
  for(unsigned dwell=0;dwell<2;dwell++){
   if(dwell&&!tracker_region_dwell(id))continue;
   for(unsigned sf=7;sf<=12;sf++)for(unsigned wide=0;wide<=2;wide++){
    tracker_radio_config_t config={.version=5,.sf=sf,.profile=1,.position_interval_s=300,.region=id,.wideband=wide,.dwell=dwell};int dr=tracker_region_dr(id,sf,wide);bool expected=dr>=0&&(!dwell||dr>=2);assert(tracker_radio_config_valid(config)==expected);if(!expected)continue;rates++;
    uint8_t actual_sf;uint16_t actual_bw;assert(tracker_region_rate(id,dr,&actual_sf,&actual_bw)&&actual_sf==sf&&actual_bw==(wide==1?500:wide==2?250:125));
    unsigned expected_max=tracker_region_payload_dwell(id,dr,dwell);assert(get(r,PHY_MAX_PAYLOAD,dr,dwell).Value==expected_max);
    VerifyParams_t v={0};v.DatarateParams.Datarate=dr;v.DatarateParams.UplinkDwellTime=dwell;if(!RegionVerify(r,&v,PHY_TX_DR)){fprintf(stderr,"Invalid production rate: id=%u sf=%u wide=%u dr=%d dwell=%u\n",id,sf,wide,dr,dwell);assert(false);}
    uint8_t ch=wide==1?64:0;TxConfigParams_t tx={.Channel=ch,.Datarate=dr,.TxPower=0,.MaxEirp=30,.AntennaGain=2.15,.PktLen=20};int8_t power;TimerTime_t duration;assert(RegionTxConfig(r,&tx,&power,&duration));assert(tx_sf==sf&&tx_bw==(wide==1?2:wide==2?1:0));assert(tracker_region_frequency(id,frequency));
    if(id==4)assert(radio_power<=11);if(id==3)assert(frequency==865062500);
   }
   assert(get(r,PHY_MIN_TX_DR,0,dwell).Value==(dwell?2:0));
   if(tracker_region_dwell(id)){AdrNextParams_t adr={.AdrEnabled=true,.Datarate=3,.TxPower=5,.AdrAckCounter=97,.UplinkDwellTime=dwell,.UpdateChanMask=true};int8_t dr,power;uint32_t ack;RegionAdrNext(r,&adr,&dr,&power,&ack);assert(dr==2);adr.Datarate=2;RegionAdrNext(r,&adr,&dr,&power,&ack);assert(dr==(dwell?2:1));}
  }
  AlternateDrParams_t alt={.NbTrials=1};int8_t join=RegionAlternateDr(r,&alt);assert(join==(id==1?4:id==2?6:tracker_region_as923(id)?2:id==0?0:5));
  for(unsigned dr=0;dr<=5;dr++){
   if(tracker_region_fsb(id))continue;
   RxConfigParams_t rx={.Channel=0,.Datarate=dr,.Bandwidth=0,.RxSlot=RX_SLOT_WIN_1};int8_t out;assert(RegionRxConfig(r,&rx,&out));assert(frequency==first[id]&&rx_sf==12-dr&&rx_bw==0);
  }
  if(id==4||tracker_region_as923(id)){
   NextChanParams_t next={.Datarate=5,.Joined=true};uint8_t ch;TimerTime_t delay,aggregated;channel_free=true;unsigned old=lbt_checks;assert(RegionNextChannel(r,&next,&ch,&delay,&aggregated));assert(lbt_checks>old&&delay==0);channel_free=false;assert(!RegionNextChannel(r,&next,&ch,&delay,&aggregated));channel_free=true;
  }
  if(tracker_region_as923(id)){
   ChannelParams_t extra={.Frequency=first[id]+400000,.DrRange.Value=0x50};ChannelAddParams_t add={.ChannelId=3,.NewChannel=&extra};assert(RegionChannelAdd(r,&add)==LORAMAC_STATUS_OK);extra.Frequency=868100000;assert(RegionChannelAdd(r,&add)!=LORAMAC_STATUS_OK);
   RxParamSetupReqParams_t setup={.Frequency=868100000,.Datarate=2};assert(!(RegionRxParamSetupReq(r,&setup)&1));setup.Frequency=rx2[id];assert((RegionRxParamSetupReq(r,&setup)&1));
  }
 }
 // Every AU FSB, alternating join rates, and ADR recovery retaining that FSB.
 for(unsigned block=0;block<=8;block++){
  LoRaMacRegion_t r=LORAMAC_REGION_AU915;RegionInitDefaults(r,INIT_TYPE_INIT);uint16_t mask[6];assert(tracker_us915_mask(block,mask));ChanMaskSetParams_t set={mask,CHANNELS_DEFAULT_MASK};assert(RegionChanMaskSet(r,&set));set.ChannelsMaskType=CHANNELS_MASK;assert(RegionChanMaskSet(r,&set));
  for(unsigned trial=1;trial<21;trial++){AlternateDrParams_t a={.NbTrials=trial};int dr=RegionAlternateDr(r,&a);assert(dr==(trial%2?6:2));NextChanParams_t next={.Datarate=dr,.Joined=false};uint8_t ch;TimerTime_t delay,aggregated;assert(RegionNextChannel(r,&next,&ch,&delay,&aggregated));assert(mask[ch/16]&(1u<<(ch%16)));}
  AdrNextParams_t a={.AdrEnabled=true,.Datarate=3,.TxPower=5,.AdrAckCounter=97,.UplinkDwellTime=1,.UpdateChanMask=true};int8_t dr,power;uint32_t ack;RegionAdrNext(r,&a,&dr,&power,&ack);assert(dr==2&&!memcmp(mask,get(r,PHY_CHANNELS_MASK,0,0).ChannelsMask,sizeof(mask)));
 }
 assert(!RegionAS923SetGroup(0)&&!RegionAS923SetGroup(5));assert(!tracker_region_frequency(4,922200000));
 printf("All 10 selectable plans: %u valid LoRa rate/dwell combinations, default channels, RX1/RX2, joins, all AU FSBs, bounded ADR, AS group frequencies and LBT busy/free decisions PASS (no RF)\n",rates);
}
