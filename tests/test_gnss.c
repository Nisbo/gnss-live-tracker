#include "gnss_nmea.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void feed(tracker_gnss_t *g,const char *body,long now) {
 unsigned cs=0;for(const char *p=body;*p;p++)cs^=(unsigned char)*p;
 char line[256];snprintf(line,sizeof(line),"$%s*%02X",body,cs);
 assert(tracker_nmea_line(g,line,now));
}
int main(void) {
 static tracker_gnss_t g;static tracker_sat_snapshot_t snapshot;uint8_t b[51];
 tracker_gnss_init(&g);tracker_position_payload(&g,0,0,b);
 assert(b[0]==2&&b[1]==0&&b[14]==255&&b[15]==255&&b[18]==255);
 assert(tracker_nmea_line(&g,"$GNGGA,,,,,,0,00,99.99,,,,,,*56",500));
 assert(tracker_nmea_line(&g,"$GNGSA,A,1,,,,,,,,,,,,,99.99,99.99,99.99,3*31",500));
 assert(tracker_nmea_line(&g,"$GPGSV,1,1,00,1*64",500));
 feed(&g,"GNGGA,120000,,,,,0,00,99.99,,,,,,",1000);
 assert(!tracker_gnss_has_fix(&g,1000));
 /* Same Galileo satellite on two bands must not count twice. */
 feed(&g,"GAGSV,1,1,02,01,40,120,35,02,30,180,20,7",2000);
 feed(&g,"GAGSV,1,1,01,01,40,120,42,1",2100);
 feed(&g,"GNGSA,A,3,01,02,,,,,,,,,,,1.1,0.8,0.9,3",2200);
 tracker_position_payload(&g,2300,1,b);
 assert(b[15]==2&&b[32]==2&&(b[25]&4)&&(b[37]&4)&&(b[38]&4));
 tracker_sat_snapshot(&g,2300,1,&snapshot);assert(snapshot.count==2);for(unsigned i=0;i<snapshot.count;i++)if(snapshot.satellites[i].id==1){assert(snapshot.satellites[i].elevation==40&&snapshot.satellites[i].azimuth==120);}
 size_t len=tracker_sat_packet(&snapshot,0,b);assert(len==15&&b[5]==2);
 bool strongest=false;
 for(int i=0;i<2;i++)if(b[7+4*i+2]==1){assert(b[7+4*i+3]==42);strongest=true;}
 assert(strongest);
 feed(&g,"GAGSV,1,1,01,01,40,120,18,1",3000);
 tracker_sat_snapshot(&g,3000,1,&snapshot);
 bool lower=false;for(int i=0;i<snapshot.count;i++)if(snapshot.satellites[i].id==1){assert(snapshot.satellites[i].cn0==18);lower=true;}
 assert(lower); /* Strength must decrease too; never retain an all-time peak. */
 /* Valid southern/western coordinates and negative altitude. */
 feed(&g,"GNGGA,120001,3354.00000,S,07036.00000,W,1,08,0.85,-12.0,M,0.0,M,,",3000);
 feed(&g,"GNRMC,120001,A,3354.00000,S,07036.00000,W,10.0,123.45,221026,,,A",3000);
 assert(tracker_gnss_has_fix(&g,3000));tracker_position_payload(&g,3000,2,b);
 assert(b[1]&2);assert(b[14]==8&&b[16]==0&&b[17]==85);
 for(int i=0;i<POSITION_BYTES;i++)printf("%02x",b[i]);puts("");
 /* A no-fix GGA invalidates the old fix immediately; silence also expires it. */
 feed(&g,"GNGGA,120002,,,,,0,00,99.99,,,,,,",4000);
 assert(!tracker_gnss_has_fix(&g,4000));tracker_position_payload(&g,4000,3,b);
 assert(!(b[1]&2)&&b[2]==0&&b[6]==0);
 assert(!tracker_nmea_line(&g,"$GAGSV,1,1,00*00",5000));
 assert(g.checksum_errors==1);
 tracker_sat_snapshot(&g,20000,4,&snapshot);assert(snapshot.count==0);
 tracker_position_payload(&g,20000,4,b);assert(b[15]==255&&b[37]==0&&b[38]==0);
 /* GA includes Galileo 1-36 and EGNOS/SBAS 37-64; never call EGNOS Galileo. */
 tracker_gnss_init(&g);
 feed(&g,"GAGSV,1,1,02,12,40,120,35,53,30,180,20,7",2000);
 feed(&g,"GLGSV,1,1,01,53,30,180,20,1",2100);
 feed(&g,"GNGSA,A,3,12,53,,,,,,,,,,,1.1,0.8,0.9,3",2200);
 tracker_position_payload(&g,2300,5,b);
 assert(b[15]==2&&b[32]==1&&b[35]==1&&(b[38]&32));
 tracker_sat_snapshot(&g,2300,5,&snapshot);assert(snapshot.count==2);
 /* More than one packet: bounds and final partial packet. */
 memset(&snapshot,0,sizeof(snapshot));snapshot.count=25;
 for(int i=0;i<25;i++){snapshot.satellites[i].system=3;snapshot.satellites[i].id=i+1;}
 assert(tracker_sat_packet(&snapshot,0,b)==51&&b[4]==3);
 assert(tracker_sat_packet(&snapshot,2,b)==19&&b[6]==3);
 assert(tracker_sat_packet(&snapshot,3,b)==0);
 tracker_gnss_init(&g);
 feed(&g,"GAGSV,1,1,03,01,90,360,40,02,,,20,03,91,361,35,7",5000);
 tracker_sat_snapshot(&g,5000,0,&snapshot);assert(snapshot.count==3);
 for(unsigned i=0;i<snapshot.count;i++){tracker_sat_view_t *s=&snapshot.satellites[i];if(s->id==1)assert(s->elevation==90&&s->azimuth==0);else assert(s->elevation==255&&s->azimuth==65535);}
 feed(&g,"GNGSA,A,3,01,,,,,,,,,,,,1,1,1,3",20000);tracker_sat_snapshot(&g,20001,0,&snapshot);assert(snapshot.count==1&&snapshot.satellites[0].elevation==255&&snapshot.satellites[0].azimuth==65535&&snapshot.satellites[0].cn0==255&&(snapshot.satellites[0].flags&SAT_USED));
 puts("GNSS parser, duplicate bands, Galileo GSA, no-fix, expiry and packet splitting: PASS");
}
