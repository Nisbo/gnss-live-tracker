from test_support import ROOT, CJSON, CC, tmp
"""Compile the production NVS regional-context routines with real region modules."""
from pathlib import Path
import subprocess
root=ROOT
port=(root/'components/loramac/lorawan_port.c').read_text();core=(root/'components/loramac/loramac/LoRaMac.c').read_text()
def function(src,signature):
 start=src.index(signature+'{') if signature+'{' in src else src.index(signature+'\n{');brace=src.index('{',start);end=brace+1;depth=1
 while depth:depth+=(src[end]=='{')-(src[end]=='}');end+=1
 return src[start:end]+'\n'
header='''#define TRACKER_RADIO_STUB_ONLY
#include "test_us915.c"
#include "RegionAS923.h"
#include "nvs.h"
#include <stddef.h>
static LoRaMacRegion_t s_region;
LoRaMacRegion_t LoRaMacRegion;
LoRaMacParams_t LoRaMacParams,LoRaMacParamsDefaults;
static uint8_t s_channel_block,s_as_group=1,s_initial_dwell;
#define LORAWAN_SESSION_NVS_NS "lorawan"
int8_t defaultDrForNoAdr;
void memcpy1(uint8_t *d,const uint8_t *s,uint16_t n){while(n--)*d++=*s++;}
uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t *p,size_t n){crc=~crc;while(n--){crc^=*p++;for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&-(int)(crc&1));}return ~crc;}
LoRaMacStatus_t LoRaMacChannelAdd(uint8_t id,ChannelParams_t p){ChannelAddParams_t a={.ChannelId=id,.NewChannel=&p};return RegionChannelAdd(s_region,&a);}
'''
for signature in ['void LoRaMacSetInitialDwell(uint8_t uplink)','void LoRaMacGetRegionalState(LoRaMacRegionalState_t *s)','bool LoRaMacRestoreRegionalState(const LoRaMacRegionalState_t *s)']:header+=function(core,signature)
for signature in ['LoRaMacRegion_t lorawan_port_region(unsigned r)','int lorawan_port_region_id(LoRaMacRegion_t region)','static uint32_t lorawan_network_plan(void)']:header+=function(port,signature)
a=port.index('typedef struct {uint32_t version,plan;');b=port.index(' regional_context_t;',a)+len(' regional_context_t;');header+=port[a:b]+'\n'
header+='''static regional_context_t stored;static bool present;
esp_err_t nvs_open(const char *name,int mode,int *h){assert(!strcmp(name,"lorawan"));(void)mode;*h=0;return ESP_OK;}
void nvs_close(int h){(void)h;}
esp_err_t nvs_get_blob(int h,const char *key,void *p,size_t *n){(void)h;assert(!strcmp(key,"context"));if(!present)return ESP_ERR_NVS_NOT_FOUND;assert(*n>=sizeof(stored));memcpy(p,&stored,sizeof(stored));*n=sizeof(stored);return ESP_OK;}
'''
for signature in ['static void regional_capture(regional_context_t *c)','static bool regional_restore(void)']:header+=function(port,signature)
header+='''static void checksum(void){stored.crc=esp_rom_crc32_le(0,(void *)&stored,offsetof(regional_context_t,crc));}
int main(void){
 for(unsigned id=0;id<TRACKER_REGION_COUNT;id++){
  s_region=LoRaMacRegion=lorawan_port_region(id);s_as_group=tracker_region_as923(id)?id-4:1;s_channel_block=tracker_region_fsb(id)?2:0;s_initial_dwell=tracker_region_dwell(id);assert(lorawan_port_region_id(s_region)==(int)id);
  if(tracker_region_as923(id))assert(RegionAS923SetGroup(s_as_group));RegionInitDefaults(s_region,INIT_TYPE_INIT);
  LoRaMacParams=(LoRaMacParams_t){.UplinkDwellTime=s_initial_dwell,.Rx1DrOffset=0,.ChannelsNbRep=1,.MaxEirp=16,.AntennaGain=2.15,.ReceiveDelay1=3000,.ReceiveDelay2=4000};
  GetPhyParams_t get={.Attribute=PHY_CHANNELS};ChannelParams_t *channels=RegionGetPhyParam(s_region,&get).Channels;
  uint32_t extra_frequency=id==0?867100000:channels[0].Frequency+600000;
  if(!tracker_region_fsb(id)){ChannelParams_t extra={.Frequency=extra_frequency,.Rx1Frequency=extra_frequency,.DrRange.Value=0x50};assert(LoRaMacChannelAdd(3,extra)==LORAMAC_STATUS_OK);}
  regional_capture(&stored);present=true;assert(stored.plan==lorawan_network_plan());
  RegionInitDefaults(s_region,INIT_TYPE_INIT);LoRaMacParams.ReceiveDelay1=1000;LoRaMacParams.UplinkDwellTime=0;assert(regional_restore());assert(LoRaMacParams.ReceiveDelay1==3000&&LoRaMacParams.ReceiveDelay2==4000&&LoRaMacParams.UplinkDwellTime==s_initial_dwell);
  if(!tracker_region_fsb(id)){channels=RegionGetPhyParam(s_region,&get).Channels;assert(channels[3].Frequency==extra_frequency&&channels[3].Rx1Frequency==extra_frequency);}
  stored.plan^=1;checksum();assert(!regional_restore());stored.plan^=1;checksum();stored.crc^=1;assert(!regional_restore());stored.crc^=1;
  stored.state.receive_delay2=99999;checksum();assert(!regional_restore());stored.state.receive_delay2=4000;
  if(stored.count){uint32_t hz=stored.channels[3].Frequency;stored.channels[3].Frequency=433000000;checksum();assert(!regional_restore());stored.channels[3].Frequency=hz;}
  present=false;assert(regional_restore()==(id==0||id==1));
 }
 puts("Production regional session context: all plans, runtime AS group binding, dynamic channels/RX1 frequencies, dwell/time/power parameters, CRC/plan rejection and EU868/US915 legacy compatibility PASS");
}
'''
path=Path(tmp('tracker-regional-session.c'));path.write_text(header)
mac='components/loramac';tests='tests';regions=['EU868','US915','AU915','IN865','KR920','AS923','RU864'];flags=[CC,'-std=c11',*['-DREGION_'+r for r in regions],'-include',tests+'/region_stubs/esp_attr.h','-I'+tests,'-I'+tests+'/region_stubs','-I'+tests+'/backup_stubs','-I'+mac,'-I'+mac+'/loramac/region','-Icomponents/sx1262']
sources=[str(path),mac+'/loramac/region/Region.c',mac+'/loramac/region/RegionCommon.c',*[mac+'/loramac/region/Region'+r+'.c' for r in regions]]
subprocess.run(flags+sources+['-lm','-o',tmp('tracker-regional-session-test')],cwd=root,check=True);subprocess.run([tmp('tracker-regional-session-test')],check=True)
