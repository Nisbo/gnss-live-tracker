#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define TRACK_SECTOR 4096
#define TRACK_SLOTS 127
#define TRACK_BREAK 1
#define TRACK_IMPORTED 2
#define TRACK_BROWSER_TIME 4
/* Versioned little-endian wire format; each record and block header is 32 bytes. */
typedef struct {uint64_t seq;int64_t utc_ms;int32_t lat_e7,lon_e7;int16_t altitude;uint8_t used,flags;uint32_t crc;} track_record_t;
_Static_assert(sizeof(track_record_t)==32,"Track record must remain 32 bytes");
typedef struct {uint64_t base;uint16_t used,count;bool valid;uint8_t first,last,kind;} track_block_t;
typedef struct {
 void *context;bool (*read)(void *,size_t,void *,size_t);bool (*write)(void *,size_t,const void *,size_t);bool (*erase)(void *,size_t,size_t);
} track_io_t;
typedef struct {int64_t min_utc,max_utc;} track_order_t;
typedef struct {track_io_t io;track_order_t *order;track_block_t *blocks;uint8_t *scratch;unsigned block_count,current;uint64_t next_seq,floor;uint32_t count;bool ready;} track_ring_t;
bool track_ring_mount(track_ring_t *,track_io_t,track_block_t *,unsigned,uint8_t *,uint64_t floor);
bool track_ring_append(track_ring_t *,track_record_t *);
size_t track_ring_read(track_ring_t *,uint64_t after,track_record_t *,size_t capacity);
void track_ring_clear(track_ring_t *);
uint64_t track_ring_oldest(const track_ring_t *);

bool track_ring_index(track_ring_t *,track_order_t *);
/* One shared store: merge skips identical UTC/coordinate points; -2 = older than full retention. */
int track_ring_merge(track_ring_t *,track_record_t *);
size_t track_ring_read_time(track_ring_t *,int64_t after_time,uint64_t after_seq,track_record_t *,size_t);
bool track_ring_find(track_ring_t *,const track_record_t *);
