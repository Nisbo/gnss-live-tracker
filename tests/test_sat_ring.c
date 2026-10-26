#include "sat_ring.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N 3
static uint8_t flash[N*SAT_SECTOR],scratch[SAT_SECTOR];static bool torn;
static bool rd(void *p,size_t off,void *out,size_t n){(void)p;assert(off+n<=sizeof(flash));memcpy(out,flash+off,n);return true;}
static bool wr(void *p,size_t off,const void *in,size_t n){(void)p;assert(off+n<=sizeof(flash));const uint8_t *src=in;size_t limit=torn?n/2:n;for(size_t i=0;i<limit;i++){assert((flash[off+i]&src[i])==src[i]);flash[off+i]&=src[i];}bool ok=!torn;torn=false;return ok;}
static bool er(void *p,size_t off,size_t n){(void)p;assert(off+n<=sizeof(flash)&&off%4096==0&&n%4096==0);memset(flash+off,255,n);return true;}
static sat_record_t point(void){return (sat_record_t){.utc_s=1792670400,.boot=42,.uptime_s=5,.visible={12,8,10,13,0,5,255},.used={8,4,5,10,0,2,255},.total=29};}
int main(void){
 memset(flash,255,sizeof(flash));track_io_t io={.read=rd,.write=wr,.erase=er};sat_ring_t ring;sat_block_t blocks[N];assert(sat_ring_mount(&ring,io,blocks,N,scratch,1));assert(ring.count==0);
 for(unsigned i=0;i<SAT_SLOTS*N+10;i++){sat_record_t p=point();assert(sat_ring_append(&ring,&p));assert(p.seq==i+1);}
 assert(ring.count==SAT_SLOTS*(N-1)+10);sat_record_t output[512];size_t count=sat_ring_read(&ring,0,output,512);assert(count==ring.count);for(size_t i=1;i<count;i++)assert(output[i].seq==output[i-1].seq+1);
 uint64_t cursor=0;size_t paged=0;while(1){size_t n=sat_ring_read(&ring,cursor,output,7);if(!n)break;for(size_t i=0;i<n;i++){assert(output[i].seq>cursor);cursor=output[i].seq;paged++;}}assert(paged==ring.count);
 uint64_t next=ring.next_seq;assert(sat_ring_mount(&ring,io,blocks,N,scratch,1));assert(ring.next_seq==next&&ring.count==count);
 torn=true;sat_record_t p=point();assert(!sat_ring_append(&ring,&p));assert(!ring.ready);
 assert(sat_ring_mount(&ring,io,blocks,N,scratch,1));p=point();p.flags=TRACK_BREAK;assert(sat_ring_append(&ring,&p));assert(p.seq==next);
 count=sat_ring_read(&ring,next-1,output,512);assert(count==1&&output[0].flags==TRACK_BREAK);
 uint64_t floor=ring.next_seq;sat_ring_clear(&ring);assert(!ring.count&&!sat_ring_read(&ring,0,output,512));assert(sat_ring_mount(&ring,io,blocks,N,scratch,floor));assert(!ring.count);
 p=point();assert(sat_ring_append(&ring,&p));assert(p.seq==floor);assert(sat_ring_mount(&ring,io,blocks,N,scratch,floor));assert(ring.count==1&&sat_ring_read(&ring,0,output,512)==1);
 /* Corrupt a written point: the CRC prevents its use after reboot. */
 size_t location=(size_t)ring.current*SAT_SECTOR+32+(blocks[ring.current].used-1)*36;flash[location+20]^=1;assert(sat_ring_mount(&ring,io,blocks,N,scratch,floor));assert(ring.count==0);
 p=point();p.utc_s=0;assert(sat_ring_append(&ring,&p));assert(sat_ring_read(&ring,p.seq-1,output,1)==1&&output[0].utc_s==0&&output[0].boot==42);
 puts("Satellite ring: append, wrap, ordered pagination, reboot recovery, torn write, persistent clear floor and CRC corruption passed");
}
