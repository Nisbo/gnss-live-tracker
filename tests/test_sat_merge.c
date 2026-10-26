#include "sat_ring.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N 5
static uint8_t flash[N*4096],scratch[4096];static bool torn;
static bool rd(void *c,size_t o,void *p,size_t n){(void)c;assert(o+n<=sizeof(flash));memcpy(p,flash+o,n);return true;}
static bool wr(void *c,size_t o,const void *p,size_t n){(void)c;const uint8_t *s=p;size_t limit=torn?n/2:n;for(size_t i=0;i<limit;i++){assert((flash[o+i]&s[i])==s[i]);flash[o+i]&=s[i];}bool ok=!torn;torn=false;return ok;}
static bool er(void *c,size_t o,size_t n){(void)c;memset(flash+o,255,n);return true;}
static sat_record_t point(unsigned seconds){return (sat_record_t){.utc_s=1792670400+seconds,.boot=42,.uptime_s=seconds,.visible={12,8,10,13,0,5,255},.used={8,4,5,10,0,2,255},.total=29,.flags=6};}
int main(void){memset(flash,255,sizeof(flash));sat_ring_t r;sat_block_t blocks[N];sat_order_t order[N];track_io_t io={.read=rd,.write=wr,.erase=er};assert(sat_ring_mount(&r,io,blocks,N,scratch,1));assert(sat_ring_index(&r,order));
 for(unsigned i=0;i<150;i++){sat_record_t p=point(i*10);assert(sat_ring_append(&r,&p));}
 sat_record_t old=point(55);old.flags|=SAT_IMPORTED;assert(sat_ring_merge(&r,&old)==1);assert(sat_ring_merge(&r,&old)==0);old.boot=99;old.uptime_s=99;assert(sat_ring_merge(&r,&old)==0);
 sat_record_t same=point(55);same.visible[0]=11;same.flags|=SAT_IMPORTED;assert(sat_ring_merge(&r,&same)==1);
 sat_record_t late=point(1600);assert(sat_ring_merge(&r,&late)==1);assert(r.count==153);
 assert(sat_ring_mount(&r,io,blocks,N,scratch,1));assert(sat_ring_index(&r,order));assert(r.count==153&&sat_ring_merge(&r,&old)==0);
 sat_record_t out[17];unsigned total=0;uint64_t cursor=0;while(1){size_t n=sat_ring_read(&r,cursor,out,17);if(!n)break;for(unsigned i=0;i<n;i++){assert(out[i].seq>cursor);cursor=out[i].seq;total++;}}assert(total==153);
 for(unsigned i=161;i<2000;i++){sat_record_t p=point(i*10);assert(sat_ring_merge(&r,&p)==1);}assert(r.count<=N*SAT_SLOTS);assert(sat_ring_merge(&r,&old)==-2);
 uint32_t count=r.count;sat_record_t bad=point(30000);bad.visible[0]=97;assert(!sat_ring_append(&r,&bad)&&r.count==count);
 torn=true;sat_record_t p=point(30000);assert(!sat_ring_append(&r,&p));assert(sat_ring_mount(&r,io,blocks,N,scratch,1));assert(sat_ring_index(&r,order));p=point(30010);assert(sat_ring_merge(&r,&p)==1);
 uint64_t floor=r.next_seq;sat_ring_clear(&r);p=point(1);assert(sat_ring_merge(&r,&p)==1);assert(order[r.current].min_utc==p.utc_s);assert(sat_ring_mount(&r,io,blocks,N,scratch,floor));assert(sat_ring_index(&r,order));assert(r.count==1);
 puts("Satellite shared store: late insertion, exact dedup, distinct values, sequence paging, reboot, full retention, invalid/torn writes and persistent clear passed");}
