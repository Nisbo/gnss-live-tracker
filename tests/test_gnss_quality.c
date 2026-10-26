#include "gnss_nmea.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static void feed(tracker_gnss_t *g,const char *body,int64_t now){unsigned cs=0;for(const char *p=body;*p;p++)cs^=(unsigned char)*p;char line[256];snprintf(line,sizeof(line),"$%s*%02X",body,cs);assert(tracker_nmea_line(g,line,now));}
static unsigned used(tracker_gnss_t *g,int64_t now){tracker_sat_snapshot_t out;tracker_sat_snapshot(g,now,0,&out);unsigned count=0;for(int i=0;i<out.count;i++)if(out.satellites[i].system==6&&(out.satellites[i].flags&SAT_USED))count++;return count;}
int main(void){tracker_gnss_t g;tracker_gnss_init(&g);
 feed(&g,"GNGST,120000.00,0.6,,,,0.3,0.4,1.2",1000);assert(g.gst_ms==1000&&fabs(hypot(g.sigma_lat,g.sigma_lon)-.5)<1e-9&&g.sigma_alt==1.2);
 feed(&g,"GNGST,120001.00,0.6,,,,,-1,",2000);assert(isnan(g.sigma_lat)&&isnan(g.sigma_lon)&&isnan(g.sigma_alt));
 feed(&g,"GNGSA,A,3,37,,,,,,,,,,,,1.0,0.8,0.9,3",3000);assert(used(&g,3000)==1);
 feed(&g,"GNGSA,A,3,,,,,,,,,,,,,1.0,0.8,0.9,3",3100);assert(used(&g,3100)==0);
 feed(&g,"GNGSA,A,3,37,,,,,,,,,,,,1.0,0.8,0.9,3",4000);feed(&g,"GNGSA,A,3,37,,,,,,,,,,,,1.0,0.8,0.9,1",4100);assert(used(&g,4100)==1);
 feed(&g,"GNGSA,A,3,,,,,,,,,,,,,1.0,0.8,0.9,3",4200);assert(used(&g,4200)==1);
 feed(&g,"GNGSA,A,3,,,,,,,,,,,,,1.0,0.8,0.9,1",4300);assert(used(&g,4300)==0);
 puts("GNSS GST and per-source SBAS GSA replacement, retention and clearing passed");}
