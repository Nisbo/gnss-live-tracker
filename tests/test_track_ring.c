#include "track_ring.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define N 3
static uint8_t flash[N*TRACK_SECTOR],scratch[TRACK_SECTOR];static bool torn;
static bool rd(void *p,size_t off,void *out,size_t n){(void)p;assert(off+n<=sizeof(flash));memcpy(out,flash+off,n);return true;}
static bool wr(void *p,size_t off,const void *in,size_t n){(void)p;assert(off+n<=sizeof(flash));const uint8_t *src=in;size_t limit=torn?n/2:n;for(size_t i=0;i<limit;i++){assert((flash[off+i]&src[i])==src[i]);flash[off+i]&=src[i];}bool ok=!torn;torn=false;return ok;}
static bool er(void *p,size_t off,size_t n){(void)p;assert(off+n<=sizeof(flash)&&off%4096==0&&n%4096==0);memset(flash+off,255,n);return true;}
static track_record_t point(void){return (track_record_t){.utc_ms=1792670400000,.lat_e7=482000000,.lon_e7=113000000,.altitude=500,.used=14};}
int main(void){
 memset(flash,255,sizeof(flash));track_io_t io={.read=rd,.write=wr,.erase=er};track_ring_t ring;track_block_t blocks[N];assert(track_ring_mount(&ring,io,blocks,N,scratch,1));assert(ring.count==0);
 for(unsigned i=0;i<TRACK_SLOTS*N+10;i++){track_record_t p=point();assert(track_ring_append(&ring,&p));assert(p.seq==i+1);}
 assert(ring.count==TRACK_SLOTS*(N-1)+10);track_record_t output[512];size_t count=track_ring_read(&ring,0,output,512);assert(count==ring.count);for(size_t i=1;i<count;i++)assert(output[i].seq==output[i-1].seq+1);
 uint64_t cursor=0;size_t paged=0;while(1){size_t n=track_ring_read(&ring,cursor,output,7);if(!n)break;for(size_t i=0;i<n;i++){assert(output[i].seq>cursor);cursor=output[i].seq;paged++;}}assert(paged==ring.count);
 uint64_t next=ring.next_seq;assert(track_ring_mount(&ring,io,blocks,N,scratch,1));assert(ring.next_seq==next&&ring.count==count);
 torn=true;track_record_t p=point();assert(!track_ring_append(&ring,&p));assert(!ring.ready);
 assert(track_ring_mount(&ring,io,blocks,N,scratch,1));p=point();p.flags=TRACK_BREAK;assert(track_ring_append(&ring,&p));assert(p.seq==next);
 count=track_ring_read(&ring,next-1,output,512);assert(count==1&&output[0].flags==TRACK_BREAK);
 uint64_t floor=ring.next_seq;track_ring_clear(&ring);assert(!ring.count&&!track_ring_read(&ring,0,output,512));assert(track_ring_mount(&ring,io,blocks,N,scratch,floor));assert(!ring.count);
 p=point();assert(track_ring_append(&ring,&p));assert(p.seq==floor);assert(track_ring_mount(&ring,io,blocks,N,scratch,floor));assert(ring.count==1&&track_ring_read(&ring,0,output,512)==1);
 /* Corrupt a written point: the CRC prevents its use after reboot. */
 size_t location=(size_t)ring.current*TRACK_SECTOR+32+(blocks[ring.current].used-1)*32;flash[location+20]^=1;assert(track_ring_mount(&ring,io,blocks,N,scratch,floor));assert(ring.count==0);
 puts("Track ring: append, wrap, ordered pagination, reboot recovery, torn write, persistent clear floor and CRC corruption passed");
}
