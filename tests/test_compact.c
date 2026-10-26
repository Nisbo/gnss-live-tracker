#include "gnss_nmea.h"
#include <assert.h>
#include <stdio.h>
#include <math.h>
static void print(const uint8_t *b){for(unsigned i=0;i<POSITION_COMPACT_BYTES;i++)printf("%02x",b[i]);puts("");}
int main(void){
 tracker_gnss_t g;uint8_t b[POSITION_COMPACT_BYTES];tracker_gnss_init(&g);
 tracker_position_compact(&g,0,65535,b);assert(b[0]==3&&b[14]==255&&b[15]==255&&b[16]==255&&b[17]==255);print(b);
 g.gga_ms=g.rmc_ms=g.nmea_ms=g.last_fix_ms=1000;g.gga_fix=g.rmc_valid=true;g.latitude=-33.9;g.longitude=-70.6;g.altitude=-12;g.hdop=.85;g.speed_mps=13.89;g.used=8;
 g.utc_confirmed=true;g.utc_received_ms=1000;g.utc_epoch_ms=1792670400000LL;g.gga_clock_ms=43200000;g.gsa_system_ms[3]=1000;
 g.satellites[0]=(tracker_sat_t){.id=1,.system=3,.seen_ms=1000,.used_ms=1000,.cn0=40};g.gsv_system_ms[3]=1000;
 tracker_position_compact(&g,1000,42,b);assert(b[1]&2);assert(b[23]==4);assert(b[16]==9);print(b);
 g.hdop=99.99;tracker_position_compact(&g,1000,43,b);assert(b[16]==254&&(b[1]&16));print(b);
 g.gga_fix=false;g.rmc_valid=false;tracker_position_compact(&g,1001,44,b);assert(!(b[1]&2)&&!b[2]&&!b[6]&&!b[19]&&b[17]==255);print(b);
 tracker_position_compact(&g,20000,45,b);assert(b[14]==255&&b[15]==255&&b[16]==255&&b[23]==0);print(b);
}
