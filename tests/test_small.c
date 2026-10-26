#include "gnss_nmea.h"
#include <assert.h>
#include <stdio.h>
static void emit(tracker_gnss_t *g,int64_t now){for(unsigned limit=8;limit<=11;limit++){uint8_t b[11];size_t n=tracker_position_small(g,now,123,limit,b);assert(n==(limit<9?0:limit<11?9:11));if(n){for(unsigned i=0;i<n;i++)printf("%02x",b[i]);puts("");}}}
int main(void){tracker_gnss_t g;tracker_gnss_init(&g);emit(&g,0);g.gga_ms=g.nmea_ms=g.rmc_ms=g.last_fix_ms=1000;g.gga_fix=g.rmc_valid=true;g.used=7;g.latitude=-33.9123456;g.longitude=-70.6123456;g.hdop=1;g.gsv_system_ms[3]=1000;g.satellites[0]=(tracker_sat_t){.id=1,.system=3,.seen_ms=1000,.used_ms=1000,.cn0=40};emit(&g,1000);g.gga_fix=false;emit(&g,1001);emit(&g,20000);return 0;}
