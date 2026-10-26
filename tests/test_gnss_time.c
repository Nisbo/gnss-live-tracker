#include "gnss_nmea.h"
#include <assert.h>
#include <stdio.h>
static void feed(tracker_gnss_t *g,const char *body,int64_t now){unsigned cs=0;for(const char *p=body;*p;p++)cs^=(unsigned char)*p;char line[256];snprintf(line,sizeof(line),"$%s*%02X",body,cs);assert(tracker_nmea_line(g,line,now));}
int main(void){
 tracker_gnss_t g;tracker_gnss_init(&g);assert(g.utc_received_ms==-1);
 feed(&g,"GNRMC,120001.125,A,,,,,0,0,221026,,,A",3000);
 assert(g.utc_epoch_ms==1792670401125LL&&g.utc_received_ms==3000&&g.utc_confirmed);
 feed(&g,"GNRMC,235959.999999,V,,,,,,,290224,,,N",4000);
 assert(g.utc_epoch_ms==1709251199999LL&&!g.utc_confirmed&&g.utc_received_ms==4000);
 feed(&g,"GNRMC,000000,A,,,,,0,0,010324,,,A",5000);
 assert(g.utc_epoch_ms==1709251200000LL&&g.utc_confirmed);
 feed(&g,"GNRMC,120000,A,,,,,0,0,290223,,,A",6000);assert(g.utc_received_ms==5000);
 feed(&g,"GNRMC,246100,A,,,,,0,0,010324,,,A",7000);assert(g.utc_received_ms==5000);
 feed(&g,"GNRMC,12x000,A,,,,,0,0,010324,,,A",8000);assert(g.utc_received_ms==5000);
 feed(&g,"GNRMC,120000,A,,,,,0,0,,,,A",9000);assert(g.utc_received_ms==5000);
 assert(tracker_nmea_line(&g,"$CFGSYS,h155155",10000));assert(g.configuration_known&&g.configuration_mask==0x155155);
 assert(!tracker_nmea_line(&g,"$CFGSYS,h-1",10000));assert(!tracker_nmea_line(&g,"$CFGSYS,h155155*00",10000));
 tracker_gnss_init(&g);assert(tracker_gnss_position_utc(&g,1000)<0);
 feed(&g,"GNRMC,235959.900,A,4800.000,N,01100.000,E,0,0,221026,,,A",1000);
 feed(&g,"GNGGA,000000.100,4800.000,N,01100.000,E,1,10,1,500,M,0,M,,",1100);
 assert(tracker_gnss_position_utc(&g,1100)==1792713600100LL);
 assert(tracker_gnss_position_utc(&g,12000)<0);
 feed(&g,"GNRMC,000000.900,A,4800.000,N,01100.000,E,0,0,231026,,,A",1200);
 feed(&g,"GNGGA,235959.100,4800.000,N,01100.000,E,1,10,1,500,M,0,M,,",1200);
 assert(tracker_gnss_position_utc(&g,1200)==1792713599100LL);
 feed(&g,"GNRMC,000001,V,,,,,,,231026,,,N",1300);assert(tracker_gnss_position_utc(&g,1300)<0);
 puts("GNSS UTC: fractional seconds, date rollover, leap year, void navigation, malformed fields passed");
}
