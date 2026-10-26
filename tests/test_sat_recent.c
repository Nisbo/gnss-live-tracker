#include "sat_ring.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define N 128
static uint8_t flash[N*SAT_SECTOR],snapshot[N*SAT_SECTOR],scratch[SAT_SECTOR];
static unsigned reads,writes,erases;
static bool rd(void *ctx,size_t off,void *out,size_t n){(void)ctx;assert(off+n<=sizeof(flash));reads++;memcpy(out,flash+off,n);return true;}
static bool wr(void *ctx,size_t off,const void *in,size_t n){(void)ctx;assert(off+n<=sizeof(flash));writes++;memcpy(flash+off,in,n);return true;}
static bool er(void *ctx,size_t off,size_t n){(void)ctx;assert(off+n<=sizeof(flash));erases++;memset(flash+off,255,n);return true;}
static int compare(const void *aa,const void *bb){const sat_record_t *a=aa,*b=bb;return a->utc_s!=b->utc_s?(a->utc_s>b->utc_s?-1:1):(a->seq>b->seq?-1:a->seq<b->seq);}
static void check(sat_ring_t *ring,uint32_t since){
 sat_record_t *all=malloc(N*SAT_SLOTS*sizeof(*all));assert(all);size_t total=sat_ring_read(ring,0,all,N*SAT_SLOTS);qsort(all,total,sizeof(*all),compare);
 memcpy(snapshot,flash,sizeof(flash));unsigned before_writes=writes,before_erases=erases;uint32_t before=UINT32_MAX,seq=UINT32_MAX;size_t seen=0;
 while(1){sat_record_t out[129];size_t count=sat_ring_read_recent(ring,before,seq,since,out,129);assert(count<=129);size_t page=count>128?128:count;
  for(size_t i=0;i<page;i++){assert(seen<total&&all[seen].utc_s>=since);assert(!memcmp(&all[seen++],&out[i],sizeof(*out)));}
  if(count<=128)break;before=out[page-1].utc_s;seq=out[page-1].seq;
 }
 while(seen<total&&all[seen].utc_s>=since){assert(!"eligible point omitted");}
 assert(writes==before_writes&&erases==before_erases&&!memcmp(snapshot,flash,sizeof(flash)));free(all);
}
int main(void){
 memset(flash,255,sizeof(flash));sat_ring_t ring;sat_block_t blocks[N];sat_order_t order[N];assert(sat_ring_mount(&ring,(track_io_t){.read=rd,.write=wr,.erase=er},blocks,N,scratch,1));assert(sat_ring_index(&ring,order));
 sat_record_t p={.boot=42,.visible={1},.total=3};p.utc_s=0;assert(sat_ring_append(&ring,&p));
 for(unsigned i=0;i<14000;i++){p.utc_s=1793000000+i/2;p.uptime_s=i;assert(sat_ring_append(&ring,&p));}
 /* Later insertion of older UTC must not appear as newest just because its sequence is high. */
 for(unsigned i=0;i<30;i++){p.utc_s=1793000100+i;p.flags=SAT_IMPORTED;assert(sat_ring_merge(&ring,&p)>=0);}
 reads=0;sat_record_t top[129];assert(sat_ring_read_recent(&ring,UINT32_MAX,UINT32_MAX,0,top,129)==129);assert(top[0].utc_s==1793006999&&!(top[0].flags&SAT_IMPORTED));assert(reads<=3);
 check(&ring,0);check(&ring,1793006000);check(&ring,1794000000);
 /* Wrap and reboot retain the same read-only ordering and exclusive tie cursor. */
 for(unsigned i=0;i<400;i++){p.utc_s=1793010000+i;p.flags=0;assert(sat_ring_append(&ring,&p));}
 assert(sat_ring_mount(&ring,(track_io_t){.read=rd,.write=wr,.erase=er},blocks,N,scratch,1)&&sat_ring_index(&ring,order));check(&ring,0);check(&ring,1793010050);
 sat_ring_clear(&ring);assert(sat_ring_read_recent(&ring,UINT32_MAX,UINT32_MAX,0,top,129)==0);p.utc_s=0;assert(sat_ring_append(&ring,&p));assert(sat_ring_read_recent(&ring,0,UINT32_MAX,0,top,129)==1&&top[0].utc_s==0);
 puts("Recent statistics: 14k points, UTC/sequence descending, old imports, ties/unknown times, bounded sector reads, wrap/reboot/clear, no flash mutations: PASS");
}
