#include "track_ring.h"
#include <string.h>
#include <limits.h>
#define TRACK_MAGIC 0x31524b54u
typedef struct {uint32_t magic;uint16_t version,size;uint64_t base;uint32_t reserved[3],crc;} track_header_t;
_Static_assert(sizeof(track_header_t)==32,"Track header must remain 32 bytes");
static uint32_t checksum(const void *data,size_t len){const uint8_t *p=data;uint32_t crc=~0u;for(size_t i=0;i<len;i++){crc^=p[i];for(int b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return ~crc;}
static bool blank(const uint8_t *p,size_t len){for(size_t i=0;i<len;i++)if(p[i]!=255)return false;return true;}
static bool record_valid(const track_record_t *p){return p->seq&&p->seq<UINT64_MAX-1&&p->utc_ms>=315532800000LL&&p->utc_ms<4102444800000LL&&p->lat_e7>=-900000000&&p->lat_e7<=900000000&&p->lon_e7>=-1800000000&&p->lon_e7<=1800000000&&p->crc==checksum(p,28);}
bool track_ring_mount(track_ring_t *r,track_io_t io,track_block_t *blocks,unsigned n,uint8_t *scratch,uint64_t floor){
 if(!r||!blocks||!scratch||!n||!io.read||!io.write||!io.erase)return false;
 *r=(track_ring_t){.io=io,.blocks=blocks,.scratch=scratch,.block_count=n,.current=n-1,.next_seq=1,.floor=floor?floor:1};memset(blocks,0,n*sizeof(*blocks));uint64_t newest_base=0;
 for(unsigned block=0;block<n;block++){
  if(!io.read(io.context,(size_t)block*TRACK_SECTOR,scratch,TRACK_SECTOR))return false;
  track_header_t header;memcpy(&header,scratch,sizeof(header));
  if(header.magic!=TRACK_MAGIC||header.version!=1||header.size!=32||!header.base||header.base>=UINT64_MAX-1||header.crc!=checksum(&header,28))continue;
  blocks[block].valid=true;blocks[block].base=header.base;
  if(header.base>newest_base){newest_base=header.base;r->current=block;}
  if(header.base>r->next_seq)r->next_seq=header.base;
  for(unsigned slot=0;slot<TRACK_SLOTS;slot++){
   const uint8_t *data=scratch+32+slot*32;if(blank(data,32))continue;
   blocks[block].used=slot+1;track_record_t point;memcpy(&point,data,sizeof(point));
   if(!record_valid(&point)||point.seq<header.base)continue;
   if(point.seq>=r->next_seq)r->next_seq=point.seq+1;
   if(point.seq>=r->floor){blocks[block].count++;r->count++;}
  }
 }
 if(r->next_seq<r->floor)r->next_seq=r->floor;
 r->ready=true;return true;
}
bool track_ring_append(track_ring_t *r,track_record_t *point){
 if(!r->ready||point->utc_ms<315532800000LL||point->utc_ms>=4102444800000LL||point->lat_e7< -900000000||point->lat_e7>900000000||point->lon_e7< -1800000000||point->lon_e7>1800000000)return false;
 unsigned block=r->current;track_block_t *meta=&r->blocks[block];
 bool new_block=!meta->valid||meta->used==TRACK_SLOTS;
 uint8_t kind=point->flags&(TRACK_IMPORTED|TRACK_BROWSER_TIME);
 if(r->order&&meta->count&&(point->utc_ms<r->order[block].max_utc||(meta->kind&0x7f)!=kind))new_block=true;
 if(new_block){
  if(r->order){
   unsigned chosen=r->block_count;for(unsigned i=0;i<r->block_count;i++){unsigned candidate=(r->current+1+i)%r->block_count;if(!r->blocks[candidate].count){chosen=candidate;break;}}
   if(chosen==r->block_count){for(unsigned i=0;i<r->block_count;i++)if(chosen==r->block_count||r->order[i].min_utc<r->order[chosen].min_utc)chosen=i;}
   block=chosen;
  }else block=(block+1)%r->block_count;
  meta=&r->blocks[block];r->count-=meta->count;*meta=(track_block_t){0};
  track_header_t header={.magic=TRACK_MAGIC,.version=1,.size=32,.base=r->next_seq};header.crc=checksum(&header,28);
  if(!r->io.erase(r->io.context,(size_t)block*TRACK_SECTOR,TRACK_SECTOR)||!r->io.write(r->io.context,(size_t)block*TRACK_SECTOR,&header,32)){r->ready=false;return false;}
  *meta=(track_block_t){.valid=true,.base=r->next_seq,.kind=(uint8_t)(kind|0x80)};r->current=block;
  if(r->order)r->order[block]=(track_order_t){0};
 }
 point->seq=r->next_seq;point->crc=checksum(point,28);if(!record_valid(point))return false;
 size_t offset=(size_t)block*TRACK_SECTOR+32+meta->used*32;
 /* Never reuse a non-erased/torn slot after reboot. */
 if(!r->io.write(r->io.context,offset,point,32)){r->ready=false;return false;}
 if(r->order){if(!meta->count){meta->first=meta->used;r->order[block].min_utc=point->utc_ms;}else if(point->utc_ms<r->order[block].max_utc)meta->kind&=0x7f;r->order[block].max_utc=point->utc_ms;meta->last=meta->used;}
 meta->used++;meta->count++;r->count++;r->next_seq++;return true;
}
size_t track_ring_read(track_ring_t *r,uint64_t after,track_record_t *out,size_t capacity){
 size_t count=0;if(!r->ready||!out||!capacity)return 0;
 uint64_t previous_base=0;
 for(unsigned step=0;step<r->block_count&&count<capacity;step++){
  unsigned block=r->block_count;for(unsigned i=0;i<r->block_count;i++)if(r->blocks[i].valid&&r->blocks[i].count&&r->blocks[i].base>previous_base&&(block==r->block_count||r->blocks[i].base<r->blocks[block].base))block=i;
  if(block==r->block_count)break;
  track_block_t *meta=&r->blocks[block];previous_base=meta->base;
  if(!meta->valid||!meta->count||meta->base+meta->used<=after)continue;
  if(!r->io.read(r->io.context,(size_t)block*TRACK_SECTOR,r->scratch,TRACK_SECTOR)){r->ready=false;return count;}
  for(unsigned slot=0;slot<meta->used&&count<capacity;slot++){
   track_record_t point;memcpy(&point,r->scratch+32+slot*32,32);
   if(record_valid(&point)&&point.seq>=meta->base&&point.seq>after&&point.seq>=r->floor)out[count++]=point;
  }
 }
 return count;
}
void track_ring_clear(track_ring_t *r){r->floor=r->next_seq;r->count=0;for(unsigned i=0;i<r->block_count;i++)r->blocks[i].count=0;}
uint64_t track_ring_oldest(const track_ring_t *r){uint64_t oldest=r->next_seq;for(unsigned i=0;i<r->block_count;i++)if(r->blocks[i].count){uint64_t base=r->blocks[i].base>r->floor?r->blocks[i].base:r->floor;if(base<oldest)oldest=base;}return oldest;}

bool track_ring_index(track_ring_t *r,track_order_t *order){
 if(!r->ready||!order)return false;
 r->order=order;memset(order,0,r->block_count*sizeof(*order));
 for(unsigned block=0;block<r->block_count;block++){
  track_block_t *m=&r->blocks[block];if(!m->count)continue;if(!r->io.read(r->io.context,(size_t)block*TRACK_SECTOR,r->scratch,TRACK_SECTOR))return false;
  bool first=true,sorted=true;int64_t last=0;uint8_t kind=0;
  for(unsigned slot=0;slot<m->used;slot++){track_record_t p;memcpy(&p,r->scratch+32+slot*32,32);if(!record_valid(&p)||p.seq<r->floor)continue;
   if(first){m->first=slot;kind=p.flags&6;order[block].min_utc=order[block].max_utc=p.utc_ms;first=false;}else{if(p.utc_ms<last)sorted=false;if((p.flags&6)!=kind)kind=64;if(p.utc_ms<order[block].min_utc)order[block].min_utc=p.utc_ms;if(p.utc_ms>order[block].max_utc)order[block].max_utc=p.utc_ms;}
   m->last=slot;last=p.utc_ms;
  }m->kind=kind|(sorted?0x80:0);
 }return true;
}
bool track_ring_find(track_ring_t *r,const track_record_t *needle){
 if(!r->ready)return false;
 for(unsigned b=0;b<r->block_count;b++){track_block_t *m=&r->blocks[b];if(!m->count)continue;if(r->order&&(needle->utc_ms<r->order[b].min_utc||needle->utc_ms>r->order[b].max_utc))continue;
  if(!r->io.read(r->io.context,(size_t)b*TRACK_SECTOR,r->scratch,TRACK_SECTOR)){r->ready=false;return false;}
  for(unsigned slot=0;slot<m->used;slot++){track_record_t p;memcpy(&p,r->scratch+32+slot*32,32);if(p.utc_ms!=needle->utc_ms||p.lat_e7!=needle->lat_e7||p.lon_e7!=needle->lon_e7)continue;if(record_valid(&p)&&p.seq>=r->floor)return true;}
 }return false;
}
int track_ring_merge(track_ring_t *r,track_record_t *p){
 if(!r->ready)return -1;
 if(track_ring_find(r,p))return 0;
 if(!r->ready)return -1;
 if(r->order){bool free=false;int64_t oldest=INT64_MAX;for(unsigned b=0;b<r->block_count;b++){if(!r->blocks[b].count)free=true;else if(r->order[b].min_utc<oldest)oldest=r->order[b].min_utc;}
  if(!free&&(r->blocks[r->current].used==TRACK_SLOTS||p->utc_ms<r->order[r->current].max_utc||(r->blocks[r->current].kind&0x7f)!=(p->flags&6))&&p->utc_ms<oldest)return -2;
 }return track_ring_append(r,p)?1:-1;
}
static int cmp_time(const track_record_t *a,const track_record_t *b){if(a->utc_ms!=b->utc_ms)return a->utc_ms<b->utc_ms?-1:1;return a->seq<b->seq?-1:a->seq>b->seq;}
static void swap_point(track_record_t *a,track_record_t *b){track_record_t p=*a;*a=*b;*b=p;}
static void heap_down(track_record_t *p,size_t count,size_t at){for(;;){size_t child=at*2+1;if(child>=count)return;if(child+1<count&&cmp_time(&p[child],&p[child+1])<0)child++;if(cmp_time(&p[at],&p[child])>=0)return;swap_point(&p[at],&p[child]);at=child;}}
size_t track_ring_read_time(track_ring_t *r,int64_t after,uint64_t after_seq,track_record_t *out,size_t capacity){
 if(!r->ready||!r->order||!capacity)return 0;
 /* Block ranges bound each page. Heap keeps the earliest K across overlapping runs. */
 size_t count=0;uint64_t visited[(r->block_count+63)/64];memset(visited,0,sizeof(visited));
 for(unsigned step=0;step<r->block_count;step++){
  unsigned block=r->block_count;for(unsigned i=0;i<r->block_count;i++){if((visited[i/64]&(1ULL<<(i%64)))||!r->blocks[i].count||r->order[i].max_utc<after)continue;if(block==r->block_count||r->order[i].min_utc<r->order[block].min_utc)block=i;}
  if(block==r->block_count)break;
  if(count==capacity&&r->order[block].min_utc>out[0].utc_ms)break;
  visited[block/64]|=1ULL<<(block%64);
  if(!r->io.read(r->io.context,(size_t)block*TRACK_SECTOR,r->scratch,TRACK_SECTOR)){r->ready=false;return 0;}
  track_block_t *m=&r->blocks[block];for(unsigned slot=m->first;slot<m->used;slot++){
   track_record_t p;memcpy(&p,r->scratch+32+slot*32,32);if(p.utc_ms<after||(p.utc_ms==after&&p.seq<=after_seq)||p.seq<r->floor)continue;if(!record_valid(&p))continue;
   if(count==capacity&&cmp_time(&p,&out[0])>=0){if(m->kind&0x80)break;continue;}
   if(count<capacity){size_t at=count++;out[at]=p;while(at&&cmp_time(&out[(at-1)/2],&out[at])<0){swap_point(&out[(at-1)/2],&out[at]);at=(at-1)/2;}}
   else{out[0]=p;heap_down(out,count,0);}
  }
 }
 for(size_t n=count;n>1;n--){swap_point(out,&out[n-1]);heap_down(out,n-1,0);}return count;
}
