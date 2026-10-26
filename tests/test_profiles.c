#include "gnss_nmea.h"
#include <assert.h>
#include <stdio.h>
static void emit(tracker_gnss_t *g,int64_t now){for(unsigned profile=0;profile<3;profile++){uint8_t b[38];size_t n=tracker_position_packet(g,now,7,profile,b);assert(n==(profile==0?23:profile==1?24:38));assert(b[0]==(profile==0?4:profile==1?3:5));for(unsigned i=0;i<n;i++)printf("%02x",b[i]);puts("");}}
int main(void){tracker_gnss_t g;tracker_gnss_init(&g);emit(&g,0);g.gga_ms=g.nmea_ms=g.rmc_ms=g.last_fix_ms=1000;g.gga_fix=g.rmc_valid=true;g.used=7;g.latitude=0.25;g.longitude=0.75;g.hdop=1;g.gsv_system_ms[3]=g.gsa_system_ms[3]=1000;g.gsv_system_ms[1]=1000;g.satellites[0]=(tracker_sat_t){.id=1,.system=3,.seen_ms=1000,.used_ms=1000,.cn0=40};g.satellites[1]=(tracker_sat_t){.id=2,.system=3,.seen_ms=1000,.used_ms=-1,.cn0=20};g.satellites[2]=(tracker_sat_t){.id=3,.system=1,.seen_ms=1000,.used_ms=-1,.cn0=30};emit(&g,1000);g.gga_fix=false;emit(&g,1001);emit(&g,20000);return 0;}
