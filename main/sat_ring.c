#include "sat_ring.h"
#include <string.h>
#include <limits.h>
#define SAT_MAGIC 0x31544153u
typedef struct {uint32_t magic;uint16_t version,size;uint64_t base;uint32_t reserved[3],crc;} sat_header_t;
_Static_assert(sizeof(sat_header_t)==32,"Track header must remain 32 bytes");
static uint32_t checksum(const void *data,size_t len){const uint8_t *p=data;uint32_t crc=~0u;for(size_t i=0;i<len;i++){crc^=p[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return ~crc;}
static bool blank(const uint8_t *p,size_t len){for(size_t i=0;i<len;i++)if(p[i]!=255)return false;return true;}
static bool record_valid(const sat_record_t *p){
 if(!p->seq||p->seq>=UINT32_MAX||p->crc!=checksum(p,32)||!p->boot||(p->utc_s&&(p->utc_s<315532800u||p->utc_s>4102444800u)))return false;
 for(unsigned i=0;i<7;i++)if((p->visible[i]>96&&p->visible[i]!=255)||(p->used[i]>96&&p->used[i]!=255))return false;
 return p->total<=96||p->total==255;
}
bool sat_ring_mount(sat_ring_t *r,track_io_t io,sat_block_t *blocks,unsigned n,uint8_t *scratch,uint64_t floor){
 if(!r||!blocks||!scratch||!n||!io.read||!io.write||!io.erase)return false;
 *r=(sat_ring_t){.io=io,.blocks=blocks,.scratch=scratch,.block_count=n,.current=n-1,.next_seq=1,.floor=floor?floor:1};memset(blocks,0,n*sizeof(*blocks));uint64_t newest_base=0;
 for(unsigned block=0;block<n;block++){
  if(!io.read(io.context,(size_t)block*SAT_SECTOR,scratch,SAT_SECTOR))return false;
  sat_header_t header;memcpy(&header,scratch,sizeof(header));
  if(header.magic!=SAT_MAGIC||header.version!=1||header.size!=36||!header.base||header.base>=UINT64_MAX-1||header.crc!=checksum(&header,28))continue;
  blocks[block].valid=true;blocks[block].base=header.base;
  if(header.base>newest_base){newest_base=header.base;r->current=block;}
  if(header.base>r->next_seq)r->next_seq=header.base;
  for(unsigned slot=0;slot<SAT_SLOTS;slot++){
   const uint8_t *data=scratch+32+slot*36;if(blank(data,36))continue;
   blocks[block].used=slot+1;sat_record_t point;memcpy(&point,data,sizeof(point));
   if(!record_valid(&point)||point.seq<header.base)continue;
   if(point.seq>=r->next_seq)r->next_seq=point.seq+1;
   if(point.seq>=r->floor){blocks[block].count++;r->count++;}
  }
 }
 if(r->next_seq<r->floor)r->next_seq=r->floor;
 r->ready=true;return true;
}
bool sat_ring_append(sat_ring_t *r,sat_record_t *point){
 if(!r->ready||r->next_seq>=UINT32_MAX)return false;
 /* Validate before retiring any existing sector. */
 sat_record_t checked=*point;checked.seq=r->next_seq;checked.crc=checksum(&checked,32);if(!record_valid(&checked))return false;
 unsigned block=r->current;sat_block_t *meta=&r->blocks[block];uint8_t kind=point->flags&SAT_IMPORTED;
 bool new_block=!meta->valid||meta->used==SAT_SLOTS;
 if(r->order&&meta->count&&((meta->kind&SAT_IMPORTED)!=kind||(point->utc_s&&point->utc_s<r->order[block].max_utc)))new_block=true;
 if(new_block){
  if(r->order){unsigned chosen=r->block_count;
   for(unsigned i=1;i<=r->block_count;i++){unsigned candidate=(r->current+i)%r->block_count;if(!r->blocks[candidate].count){chosen=candidate;break;}}
   if(chosen==r->block_count){for(unsigned i=0;i<r->block_count;i++)if(r->order[i].min_utc&&(chosen==r->block_count||r->order[i].min_utc<r->order[chosen].min_utc))chosen=i;}
   if(chosen==r->block_count){for(unsigned i=0;i<r->block_count;i++)if(chosen==r->block_count||r->blocks[i].base<r->blocks[chosen].base)chosen=i;}
   block=chosen;
  }else block=(block+1)%r->block_count;
  meta=&r->blocks[block];r->count-=meta->count;*meta=(sat_block_t){0};
  sat_header_t header={.magic=SAT_MAGIC,.version=1,.size=36,.base=r->next_seq};header.crc=checksum(&header,28);
  if(!r->io.erase(r->io.context,(size_t)block*SAT_SECTOR,SAT_SECTOR)||!r->io.write(r->io.context,(size_t)block*SAT_SECTOR,&header,32)){r->ready=false;return false;}
  *meta=(sat_block_t){.valid=true,.base=r->next_seq,.kind=kind};r->current=block;if(r->order)r->order[block]=(sat_order_t){0};
 }
 point->seq=r->next_seq;point->crc=checksum(point,32);if(!record_valid(point))return false;
 size_t offset=(size_t)block*SAT_SECTOR+32+meta->used*36;
 /* Never reuse a non-erased/torn slot after reboot. */
 if(!r->io.write(r->io.context,offset,point,36)){r->ready=false;return false;}
 if(r->order&&!meta->count){r->order[block]=(sat_order_t){0};meta->kind=kind;}
 if(r->order&&point->utc_s){sat_order_t *o=&r->order[block];if(!o->min_utc||point->utc_s<o->min_utc)o->min_utc=point->utc_s;if(point->utc_s>o->max_utc)o->max_utc=point->utc_s;}
 meta->used++;meta->count++;r->count++;r->next_seq++;return true;
}
size_t sat_ring_read(sat_ring_t *r,uint64_t after,sat_record_t *out,size_t capacity){
 size_t count=0;if(!r->ready||!out||!capacity)return 0;
 uint64_t previous_base=0;
 for(unsigned step=0;step<r->block_count&&count<capacity;step++){
  unsigned block=r->block_count;for(unsigned i=0;i<r->block_count;i++)if(r->blocks[i].count&&r->blocks[i].base>previous_base&&(block==r->block_count||r->blocks[i].base<r->blocks[block].base))block=i;
  if(block==r->block_count)break;
  sat_block_t *meta=&r->blocks[block];previous_base=meta->base;
  if(!meta->valid||!meta->count||meta->base+meta->used<=after)continue;
  if(!r->io.read(r->io.context,(size_t)block*SAT_SECTOR,r->scratch,SAT_SECTOR)){r->ready=false;return count;}
  for(unsigned slot=0;slot<meta->used&&count<capacity;slot++){
   sat_record_t point;memcpy(&point,r->scratch+32+slot*36,36);
   if(record_valid(&point)&&point.seq>=meta->base&&point.seq>after&&point.seq>=r->floor)out[count++]=point;
  }
 }
 return count;
}
static bool sat_newer(const sat_record_t *a,const sat_record_t *b){
 return a->utc_s>b->utc_s||(a->utc_s==b->utc_s&&a->seq>b->seq);
}
size_t sat_ring_read_recent(sat_ring_t *r,uint32_t before_utc,uint32_t before_seq,
                           uint32_t since_utc,sat_record_t *out,size_t capacity){
 if(!r||!r->ready||!out||!capacity)return 0;
 size_t count=0;uint32_t prior_time=UINT32_MAX;uint64_t prior_base=UINT64_MAX;
 for(unsigned step=0;step<r->block_count;step++){
  unsigned block=r->block_count;uint32_t best_time=0;uint64_t best_base=0;
  for(unsigned i=0;i<r->block_count;i++){
   sat_block_t *m=&r->blocks[i];if(!m->valid||!m->count)continue;
   uint32_t latest=r->order?r->order[i].max_utc:0;
   if(latest>prior_time||(latest==prior_time&&m->base>=prior_base))continue;
   if(block==r->block_count||latest>best_time||(latest==best_time&&m->base>best_base)){block=i;best_time=latest;best_base=m->base;}
  }
  if(block==r->block_count)break;
  prior_time=best_time;prior_base=best_base;
  if(r->order&&(best_time<since_utc||(count==capacity&&best_time<out[count-1].utc_s)))continue;
  if(!r->io.read(r->io.context,(size_t)block*SAT_SECTOR,r->scratch,SAT_SECTOR)){r->ready=false;return count;}
  sat_block_t *m=&r->blocks[block];
  for(unsigned slot=0;slot<m->used;slot++){
   sat_record_t p;memcpy(&p,r->scratch+32+slot*36,36);
   if(!record_valid(&p)||p.seq<m->base||p.seq<r->floor||p.utc_s<since_utc||
      p.utc_s>before_utc||(p.utc_s==before_utc&&p.seq>=before_seq))continue;
   size_t at=0;while(at<count&&sat_newer(&out[at],&p))at++;
   if(at>=capacity)continue;
   if(count<capacity)count++;
   if(at+1<count)memmove(out+at+1,out+at,(count-at-1)*sizeof(*out));
   out[at]=p;
  }
 }
 return count;
}
void sat_ring_clear(sat_ring_t *r){r->floor=r->next_seq;r->count=0;for(unsigned i=0;i<r->block_count;i++)r->blocks[i].count=0;if(r->order)memset(r->order,0,r->block_count*sizeof(*r->order));}
uint64_t sat_ring_oldest(const sat_ring_t *r){uint64_t oldest=r->next_seq;for(unsigned i=0;i<r->block_count;i++)if(r->blocks[i].count){uint64_t base=r->blocks[i].base>r->floor?r->blocks[i].base:r->floor;if(base<oldest)oldest=base;}return oldest;}

bool sat_ring_index(sat_ring_t *r,sat_order_t *order){
 if(!r->ready||!order)return false;
 r->order=order;memset(order,0,r->block_count*sizeof(*order));
 for(unsigned b=0;b<r->block_count;b++){sat_block_t *m=&r->blocks[b];if(!m->count)continue;if(!r->io.read(r->io.context,(size_t)b*SAT_SECTOR,r->scratch,SAT_SECTOR)){r->ready=false;return false;}
  bool first=true;for(unsigned i=0;i<m->used;i++){sat_record_t p;memcpy(&p,r->scratch+32+i*36,36);if(!record_valid(&p)||p.seq<r->floor||p.seq<m->base)continue;if(first){m->kind=p.flags&SAT_IMPORTED;first=false;}
   if(p.utc_s){if(!order[b].min_utc||p.utc_s<order[b].min_utc)order[b].min_utc=p.utc_s;if(p.utc_s>order[b].max_utc)order[b].max_utc=p.utc_s;}
  }
 }return true;
}
int sat_ring_merge(sat_ring_t *r,sat_record_t *p){
 if(!r->ready||!r->order||!p->utc_s)return -1;
 uint32_t oldest=UINT32_MAX;bool free_block=false;
 for(unsigned b=0;b<r->block_count;b++){sat_block_t *m=&r->blocks[b];sat_order_t *o=&r->order[b];if(!m->count){free_block=true;continue;}
  if(o->min_utc&&o->min_utc<oldest)oldest=o->min_utc;
  if(!o->min_utc||p->utc_s<o->min_utc||p->utc_s>o->max_utc)continue;
  if(!r->io.read(r->io.context,(size_t)b*SAT_SECTOR,r->scratch,SAT_SECTOR)){r->ready=false;return -1;}
  for(unsigned i=0;i<m->used;i++){sat_record_t q;memcpy(&q,r->scratch+32+i*36,36);if(q.utc_s==p->utc_s&&q.total==p->total&&((q.flags^p->flags)&6)==0&&!memcmp(q.visible,p->visible,7)&&!memcmp(q.used,p->used,7)&&record_valid(&q)&&q.seq>=r->floor&&q.seq>=m->base)return 0;}
 }
 if(!free_block&&p->utc_s<oldest)return -2;
 return sat_ring_append(r,p)?1:-1;
}
