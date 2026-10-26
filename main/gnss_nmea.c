#include "gnss_nmea.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
static bool recent(int64_t stamp, int64_t now, int64_t limit) {
    return stamp>=0 && now>=stamp && now-stamp<limit;
}
void tracker_gnss_init(tracker_gnss_t *g) {
    memset(g,0,sizeof(*g));
    g->gga_ms=g->rmc_ms=g->last_fix_ms=g->nmea_ms=g->gsa_ms=g->utc_received_ms=-1;
    g->hdop=g->altitude=g->speed_mps=g->course=NAN;
    g->sigma_lat=g->sigma_lon=g->sigma_alt=NAN;g->gst_ms=-1;g->gga_clock_ms=-1;
    for(int i=0;i<8;i++) g->gsa_system_ms[i]=g->gsv_system_ms[i]=-1;
    for(int i=0;i<TRACKER_MAX_SATS;i++) g->satellites[i].seen_ms=g->satellites[i].used_ms=-1;
}
static uint8_t system_for(const char *talker) {
    if(!strncmp(talker,"GP",2)) return 1;
    if(!strncmp(talker,"GL",2)) return 2;
    if(!strncmp(talker,"GA",2)) return 3;
    if(!strncmp(talker,"GB",2)||!strncmp(talker,"BD",2)) return 4;
    if(!strncmp(talker,"GQ",2)) return 5;
    if(!strncmp(talker,"SB",2)) return 6;
    if(!strncmp(talker,"GI",2)) return 7;
    return 0; /* GN is mixed; do not guess a constellation. */
}
static uint8_t satellite_system(uint8_t system,int id) {
    /* Unicore UFirebirdII Table 1-2: SBAS is mixed into each talker's GSV/GSA. */
    if((system==1||system==2||system==5||system==7)&&id>=33&&id<=64)return 6;
    if(system==3&&id>=37&&id<=64)return 6; /* Galileo + EGNOS */
    if(system==4&&id>=65&&id<=79)return 6; /* BeiDou SBAS */
    return system;
}
static tracker_sat_t *sat_slot(tracker_gnss_t *g,uint8_t system,uint16_t id,int64_t now) {
    tracker_sat_t *empty=NULL;
    for(int i=0;i<TRACKER_MAX_SATS;i++) {
        tracker_sat_t *s=&g->satellites[i];
        if(s->id==id && s->system==system) return s;
        if(!s->id || (!recent(s->seen_ms,now,15000)&&!recent(s->used_ms,now,15000))) empty=s;
    }
    if(!empty) { g->overflow=true; return NULL; }
    *empty=(tracker_sat_t){.system=system,.id=id,.cn0=255,.elevation=255,.azimuth=65535,.seen_ms=-1,.used_ms=-1};
    return empty;
}
static double number(const char *s) {
    if(!s||!*s) return NAN;
    char *end; double value=strtod(s,&end);
    return *end==0 && isfinite(value)?value:NAN;
}
static double coordinate(const char *s,const char *hemisphere,bool latitude) {
    double v=number(s); if(!isfinite(v)||v<0) return NAN;
    int degrees=(int)(v/100); double minutes=v-degrees*100;
    if(minutes>=60 || strlen(hemisphere)!=1) return NAN;
    char h=hemisphere[0];
    if(latitude ? (h!='N'&&h!='S') : (h!='E'&&h!='W')) return NAN;
    double result=degrees+minutes/60;
    if(result>(latitude?90:180)) return NAN;
    return (h=='S'||h=='W')?-result:result;
}
/* RMC reports UTC date/time. Keep receiver validity separate from the clock
   value: a void navigation sentence may still report a plausible UTC time. */
static bool leap_year(int year) {return year%4==0&&(year%100!=0||year%400==0);}
static int two_digits(const char *p) {return (p[0]-'0')*10+p[1]-'0';}
static int64_t rmc_utc(const char *date,const char *clock) {
    if(strlen(date)!=6||strlen(clock)<6)return -1;
    for(int i=0;i<6;i++)if(date[i]<'0'||date[i]>'9'||clock[i]<'0'||clock[i]>'9')return -1;
    if(clock[6]&&clock[6]!='.')return -1;
    int fraction=0,multiplier=100;
    for(size_t i=7;clock[6]&&i<strlen(clock);i++) {
        if(clock[i]<'0'||clock[i]>'9')return -1;
        if(multiplier){fraction+=(clock[i]-'0')*multiplier;multiplier/=10;}
    }
    int day=two_digits(date),month=two_digits(date+2),year=two_digits(date+4);year+=year>=80?1900:2000;
    int hour=two_digits(clock),minute=two_digits(clock+2),second=two_digits(clock+4);
    static const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(month<1||month>12||day<1||day>lengths[month-1]+(month==2&&leap_year(year))||hour>23||minute>59||second>59)return -1;
    int64_t days=0;for(int y=1970;y<year;y++)days+=leap_year(y)?366:365;
    for(int m=1;m<month;m++){days+=lengths[m-1]+(m==2&&leap_year(year));}
    days+=day-1;
    return ((days*24+hour)*60*60+minute*60+second)*1000+fraction;
}
bool tracker_nmea_line(tracker_gnss_t *g,const char *line,int64_t now) {
    if(!line||line[0]!='$'||strlen(line)>=256) return false;
    if(!strncmp(line,"$CFGSYS,",8)) {
        const char *value=line+8;if(*value=='h'||*value=='H')value++;
        if(!((*value>='0'&&*value<='9')||(*value>='a'&&*value<='f')||(*value>='A'&&*value<='F')))return false;
        char *end;unsigned long mask=strtoul(value,&end,16);
        if(end==value||mask>UINT32_MAX)return false;
        if(*end=='*'){unsigned expected;int consumed=0;uint8_t actual=0;for(const char *q=line+1;q<end;q++)actual^=(uint8_t)*q;if(strlen(end)!=3||sscanf(end+1,"%2x%n",&expected,&consumed)!=1||consumed!=2||actual!=expected)return false;}
        else if(*end)return false;
        g->configuration_known=true;g->configuration_mask=(uint32_t)mask;return true;
    }
    const char *star=strchr(line,'*');
    if(!star || strlen(star)!=3) return false;
    unsigned expected; int n=0;
    if(sscanf(star+1,"%2x%n",&expected,&n)!=1||n!=2) return false;
    uint8_t checksum=0;
    for(const char *p=line+1;p<star;p++) checksum^=(uint8_t)*p;
    if(checksum!=expected) { g->checksum_errors++; return false; }
    char buf[256]; memcpy(buf,line+1,(size_t)(star-line-1)); buf[star-line-1]=0;
    char *t[40]; int count=0; char *p=buf;
    do { t[count++]=p; char *comma=strchr(p,','); if(!comma) break; *comma=0;p=comma+1; } while(count<40);
    if(strlen(t[0])!=5) return false;
    const char *type=t[0]+2; uint8_t system=system_for(t[0]);
    g->sentences++; g->nmea_ms=now;
    if(!strcmp(type,"GGA")&&count>=11) {
        g->gga_ms=now;int64_t time=rmc_utc("010100",t[1]);g->gga_clock_ms=time<0?-1:time%86400000; g->quality=(uint8_t)atoi(t[6]); g->used=(uint8_t)atoi(t[7]);
        g->hdop=number(t[8]); g->altitude=number(t[9]);
        double lat=coordinate(t[2],t[3],true),lon=coordinate(t[4],t[5],false);
        g->gga_fix=g->quality>0 && isfinite(lat)&&isfinite(lon);
        if(g->gga_fix) { g->latitude=lat;g->longitude=lon;g->last_fix_ms=now; }
    } else if(!strcmp(type,"RMC")&&count>=10) {
        g->rmc_ms=now;g->rmc_valid=!strcmp(t[2],"A");
        int64_t utc=rmc_utc(t[9],t[1]);if(utc>=0){g->utc_epoch_ms=utc;g->utc_received_ms=now;g->utc_confirmed=g->rmc_valid;}
        g->speed_mps=number(t[7])*0.514444444;g->course=number(t[8]);
    } else if(!strcmp(type,"GST")&&count>=9) {
        g->gst_ms=now;g->sigma_lat=number(t[6]);g->sigma_lon=number(t[7]);g->sigma_alt=number(t[8]);
        if(g->sigma_lat<0)g->sigma_lat=NAN;
        if(g->sigma_lon<0)g->sigma_lon=NAN;
        if(g->sigma_alt<0)g->sigma_alt=NAN;
    } else if(!strcmp(type,"GSA")&&count>=18) {
        /* NMEA 4.1 system ID is authoritative for mixed GN talkers. */
        if(count>18&&*t[18]) {
            int id=atoi(t[18]);
            static const uint8_t ids[]={0,1,2,3,4,5,7};
            if(id>=1&&id<=6) system=ids[id];
        }
        if(system) {
            g->gsa_ms=now;
            g->gsa_system_ms[system]=now;
            /* GSA replaces this source's previous list, including SBAS entries
               mapped out of that constellation. Other sources remain independent. */
            for(int i=0;i<TRACKER_MAX_SATS;i++) {
                tracker_sat_t *sat=&g->satellites[i];sat->used_sources&=~(1u<<system);
                if(!sat->used_sources)sat->used_ms=-1;
            }
            if(atoi(t[2])>=2) for(int i=3;i<=14;i++) {
                int id=atoi(t[i]); if(id<=0||id>65535) continue;
                uint8_t resolved=satellite_system(system,id);
                if(resolved==6)g->gsa_system_ms[6]=now;
                tracker_sat_t *s=sat_slot(g,resolved,(uint16_t)id,now);
                if(s){s->used_ms=now;s->used_sources|=1u<<system;}
            }
        }
    } else if(!strcmp(type,"GSV")&&count>=4&&system) {
        g->gsv_system_ms[system]=now;
        for(int i=4;i+3<count;i+=4) {
            int id=atoi(t[i]);if(id<=0||id>65535) continue;
            uint8_t sat_system=satellite_system(system,id);
            g->gsv_system_ms[sat_system]=now;
            tracker_sat_t *s=sat_slot(g,sat_system,(uint16_t)id,now);
            if(!s) continue;
            double cn0=number(t[i+3]);
            uint8_t strength=isfinite(cn0)&&cn0>=0&&cn0<=99?(uint8_t)cn0:255;
            /* Multiple signal bands of the same satellite count only once. */
            if(s->cn0_epoch!=now/1000||s->cn0==255) s->cn0=strength;
            else if(strength!=255&&strength>s->cn0) s->cn0=strength;
            s->cn0_epoch=now/1000;
            double elevation=number(t[i+1]),azimuth=number(t[i+2]);
            s->elevation=isfinite(elevation)&&elevation>=0&&elevation<=90&&floor(elevation)==elevation?(uint8_t)elevation:255;
            s->azimuth=isfinite(azimuth)&&azimuth>=0&&azimuth<=360&&floor(azimuth)==azimuth?(uint16_t)((unsigned)azimuth%360):65535;
            s->seen_ms=now;
        }
    }
    return true;
}
bool tracker_gnss_has_fix(const tracker_gnss_t *g,int64_t now) {
    return g->gga_fix && recent(g->gga_ms,now,10000);
}
static void put16(uint8_t *b,uint16_t n) { b[0]=(uint8_t)(n>>8);b[1]=(uint8_t)n; }
static void put32(uint8_t *b,uint32_t n) { for(int i=3;i>=0;i--) {b[i]=(uint8_t)n;n>>=8;} }
static uint16_t scaled(double n,double scale) {
    return isfinite(n)&&n>=0&&n*scale<65534.5?(uint16_t)lround(n*scale):65535;
}
void tracker_sat_snapshot(const tracker_gnss_t *g,int64_t now,uint16_t seq,tracker_sat_snapshot_t *out) {
    memset(out,0,sizeof(*out));out->sequence=seq;
    for(int i=1;i<=7;i++) if(recent(g->gsa_system_ms[i],now,15000)) out->usage_known_mask|=1<<(i-1);
    for(int i=0;i<TRACKER_MAX_SATS;i++) {
        tracker_sat_t s=g->satellites[i];
        bool visible=recent(s.seen_ms,now,15000),used=false;
        if(s.used_sources){for(int source=1;source<8;source++)if((s.used_sources&(1u<<source))&&recent(g->gsa_system_ms[source],now,15000))used=true;}
        else used=recent(s.used_ms,now,15000);
        if(!s.id||(!visible&&!used)) continue;
        out->satellites[out->count++]=(tracker_sat_view_t){.id=s.id,.system=s.system,
            .cn0=visible?s.cn0:255,.elevation=visible?s.elevation:255,.azimuth=visible?s.azimuth:65535,
            .flags=(visible?SAT_VISIBLE:0)|(used?SAT_USED:0)};
    }
}
void tracker_position_payload(const tracker_gnss_t *g,int64_t now,uint16_t seq,uint8_t b[POSITION_BYTES]) {
    memset(b,0,POSITION_BYTES);bool fix=tracker_gnss_has_fix(g,now);
    bool data=recent(g->nmea_ms,now,10000);static tracker_sat_snapshot_t snapshot;
    tracker_sat_snapshot(g,now,seq,&snapshot);
    b[0]=2;b[1]=(fix?2:0)|(data?4:0)|(g->overflow?8:0)|(snapshot.usage_known_mask?32:0);
    if(fix) {
        put32(b+2,(uint32_t)(int32_t)llround(g->latitude*1e7));
        put32(b+6,(uint32_t)(int32_t)llround(g->longitude*1e7));
    }
    int16_t alt=fix&&isfinite(g->altitude)&&g->altitude>=-32767&&g->altitude<=32767?(int16_t)lround(g->altitude):INT16_MIN;
    put16(b+10,(uint16_t)alt);put16(b+12,seq);
    b[14]=recent(g->gga_ms,now,10000)?g->used:255;
    b[15]=255;
    for(int i=1;i<=7;i++) if(recent(g->gsv_system_ms[i],now,15000)) { b[38]|=1<<(i-1);b[15]=0; }
    b[37]=snapshot.usage_known_mask;
    for(int i=0;i<snapshot.count;i++) {
        tracker_sat_view_t *s=&snapshot.satellites[i];
        if(s->flags&SAT_VISIBLE) { if(b[15]==255)b[15]=0; b[15]++;if(s->system>=1&&s->system<=7)b[29+s->system]++; }
        if(s->flags&SAT_USED) b[25]|=1<<(s->system-1);
    }
    put16(b+16,recent(g->gga_ms,now,10000)?scaled(g->hdop,100):65535);
    put16(b+18,g->last_fix_ms>=0?(uint16_t)fmin((now-g->last_fix_ms)/1000,65534):65535);
    bool motion=fix&&g->rmc_valid&&recent(g->rmc_ms,now,10000);
    put16(b+20,motion?scaled(g->speed_mps,100):65535);
    put16(b+22,motion?scaled(g->course,100):65535);
    b[24]=recent(g->gga_ms,now,10000)?g->quality:255;put32(b+26,(uint32_t)(now/1000));
}
size_t tracker_sat_packet(const tracker_sat_snapshot_t *s,uint8_t chunk,uint8_t b[51]) {
    unsigned chunks=s->count?(s->count+SATS_PER_PACKET-1)/SATS_PER_PACKET:1;
    if(chunk>=chunks) return 0;
    unsigned start=chunk*SATS_PER_PACKET,n=s->count>start?s->count-start:0;
    if(n>SATS_PER_PACKET)n=SATS_PER_PACKET;
    b[0]=1;put16(b+1,s->sequence);b[3]=chunk+1;b[4]=chunks;b[5]=s->count;b[6]=n;
    for(unsigned i=0;i<n;i++) {
        const tracker_sat_view_t *sat=&s->satellites[start+i];size_t k=7+4*i;
        b[k]=sat->system|((sat->flags&SAT_USED)?128:0)|((sat->flags&SAT_VISIBLE)?64:0)|((s->usage_known_mask&(1<<(sat->system-1)))?32:0);
        put16(b+k+1,sat->id);b[k+3]=sat->cn0;
    }
    return 7+4*n;
}

int64_t tracker_gnss_position_utc(const tracker_gnss_t *g,int64_t now){
 if(!tracker_gnss_has_fix(g,now)||g->gga_clock_ms<0||!g->utc_confirmed||g->utc_received_ms<0||now<g->utc_received_ms||now-g->utc_received_ms>=10000)return -1;
 int64_t t=g->utc_epoch_ms/86400000*86400000+g->gga_clock_ms;
 if(t-g->utc_epoch_ms>43200000)t-=86400000;else if(g->utc_epoch_ms-t>43200000)t+=86400000;
 return t;
}

/* v3, 24 bytes: version/flags, lat/lon e7, altitude, sequence, used/reported,
   HDOP in tenths, speed cm/s, GNSS measurement UTC seconds, systems-used mask.
   No position/UTC is invented when the receiver has no valid fix. */
void tracker_position_compact(const tracker_gnss_t *g,int64_t now,uint16_t seq,uint8_t b[POSITION_COMPACT_BYTES]){
 uint8_t legacy[POSITION_BYTES];tracker_position_payload(g,now,seq,legacy);
 memset(b,0,POSITION_COMPACT_BYTES);b[0]=3;b[1]=legacy[1];memcpy(b+2,legacy+2,14);
 uint16_t hdop=((uint16_t)legacy[16]<<8)|legacy[17];
 if(hdop!=65535&&hdop>2540)b[1]|=16;
 b[16]=hdop==65535?255:(uint8_t)fmin(254,lround(hdop/10.0));
 memcpy(b+17,legacy+20,2);int64_t utc=tracker_gnss_position_utc(g,now);
 put32(b+19,utc>=0?(uint32_t)(utc/1000):0);b[23]=legacy[25];
}

size_t tracker_position_packet(const tracker_gnss_t *g,int64_t now,uint16_t seq,unsigned profile,uint8_t b[38]) {
 tracker_position_compact(g,now,seq,b);
 if(profile==0){b[0]=4;b[1]&=~32;return 23;}
 if(profile!=2)return 24;
 b[0]=5;memset(b+24,255,14);
 tracker_sat_snapshot_t snapshot;tracker_sat_snapshot(g,now,seq,&snapshot);
 for(unsigned system=1;system<=7;system++){
  unsigned v=0,u=0;
  for(unsigned i=0;i<snapshot.count;i++)if(snapshot.satellites[i].system==system){v+=!!(snapshot.satellites[i].flags&SAT_VISIBLE);u+=!!(snapshot.satellites[i].flags&SAT_USED);}
  if(recent(g->gsv_system_ms[system],now,15000))b[24+(system-1)*2]=v;
  if(snapshot.usage_known_mask&(1<<(system-1)))b[25+(system-1)*2]=u;
 }
 return 38;
}

/* 0x6x: 11 bytes with totals; 0x7x: 9 bytes when MAC commands consume two bytes.
   Both retain the full signed 1e-7-degree coordinate precision. */
size_t tracker_position_small(const tracker_gnss_t *g,int64_t now,uint16_t seq,unsigned limit,uint8_t b[11]){
 uint8_t normal[24];tracker_position_compact(g,now,seq,normal);
 unsigned n=limit>=11?11:limit>=9?9:0;if(!n)return 0;
 b[0]=(n==11?0x60:0x70)|(normal[1]&14);memcpy(b+1,normal+2,8);if(n==11){b[9]=normal[14];b[10]=normal[15];}return n;
}
