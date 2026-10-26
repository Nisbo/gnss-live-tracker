#include "track_ring.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N 4
static uint8_t flash[N*4096],scratch[4096];
static bool rd(void *c,size_t o,void *p,size_t n){(void)c;assert(o+n<=sizeof(flash));memcpy(p,flash+o,n);return true;}
static bool wr(void *c,size_t o,const void *p,size_t n){(void)c;const uint8_t *s=p;for(size_t i=0;i<n;i++){assert((flash[o+i]&s[i])==s[i]);flash[o+i]&=s[i];}return true;}
static bool er(void *c,size_t o,size_t n){(void)c;memset(flash+o,255,n);return true;}
static track_record_t point(int seconds,int coord){return (track_record_t){.utc_ms=1792670400000LL+(int64_t)seconds*1000,.lat_e7=480000000+coord,.lon_e7=110000000,.altitude=500};}
int main(void){memset(flash,255,sizeof(flash));track_ring_t r;track_block_t blocks[N];track_order_t order[N];track_io_t io={.read=rd,.write=wr,.erase=er};assert(track_ring_mount(&r,io,blocks,N,scratch,1));assert(track_ring_index(&r,order));
 for(int i=0;i<170;i++){track_record_t p=point(i*10,i);assert(track_ring_append(&r,&p));}
 track_record_t imported=point(55,999);imported.flags=TRACK_IMPORTED|TRACK_BREAK;assert(track_ring_merge(&r,&imported)==1);assert(track_ring_merge(&r,&imported)==0);assert(r.count==171);
 track_record_t next=point(1800,180);assert(track_ring_append(&r,&next));assert(r.count==172);
 track_record_t out[32];int64_t t=0;uint64_t seq=0;size_t total=0;bool saw=false;while(1){size_t n=track_ring_read_time(&r,t,seq,out,32);if(!n)break;for(size_t i=0;i<n;i++){assert(out[i].utc_ms>t||(out[i].utc_ms==t&&out[i].seq>seq));t=out[i].utc_ms;seq=out[i].seq;saw|=out[i].lat_e7==480000999;total++;}}assert(total==172&&saw);
 assert(track_ring_mount(&r,io,blocks,N,scratch,1));assert(track_ring_index(&r,order));assert(r.count==172&&track_ring_find(&r,&imported));
 for(int i=181;i<1000;i++){track_record_t p=point(i*10,i);assert(track_ring_append(&r,&p));}assert(r.count<=N*TRACK_SLOTS);t=0;seq=0;total=0;while(1){size_t n=track_ring_read_time(&r,t,seq,out,32);if(!n)break;for(size_t i=0;i<n;i++){assert(out[i].utc_ms>t||(out[i].utc_ms==t&&out[i].seq>seq));t=out[i].utc_ms;seq=out[i].seq;total++;}}assert(total==r.count);
 uint64_t after=0;total=0;while(1){size_t n=track_ring_read(&r,after,out,32);if(!n)break;for(size_t i=0;i<n;i++){assert(out[i].seq>after);after=out[i].seq;total++;}}assert(total==r.count);
 puts("Unified track: late insertion, deduplication, chronological/sequence pagination, reboot and chronological block retention passed");}
