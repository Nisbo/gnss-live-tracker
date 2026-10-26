#pragma once
#include "track_ring.h"
#define SAT_SECTOR 4096
#define SAT_SLOTS 112
#define SAT_IMPORTED 8
#define SAT_TIME_DERIVED 16
typedef struct {uint32_t min_utc,max_utc;} sat_order_t;
/* 36-byte versioned record. UTC zero means unknown; uptime/boot preserve order. */
typedef struct {uint32_t seq,utc_s,uptime_s,boot;uint8_t visible[7],used[7],total,flags;uint32_t crc;} sat_record_t;
_Static_assert(sizeof(sat_record_t)==36,"Satellite record size");
typedef track_block_t sat_block_t;
typedef struct {track_io_t io;sat_order_t *order;sat_block_t *blocks;uint8_t *scratch;unsigned block_count,current;uint64_t next_seq,floor;uint32_t count;bool ready;} sat_ring_t;
bool sat_ring_mount(sat_ring_t *,track_io_t,sat_block_t *,unsigned,uint8_t *,uint64_t);
bool sat_ring_append(sat_ring_t *,sat_record_t *);
size_t sat_ring_read(sat_ring_t *,uint64_t after,sat_record_t *,size_t capacity);
void sat_ring_clear(sat_ring_t *);
uint64_t sat_ring_oldest(const sat_ring_t *);

bool sat_ring_index(sat_ring_t *,sat_order_t *);
/* 1 added, 0 identical UTC/values, -2 older than full retention, -1 I/O. */
int sat_ring_merge(sat_ring_t *,sat_record_t *);

/* UTC descending, then sequence descending. Exclusive composite cursor; UTC=0
 * records sort last. No flash writes or additional scratch buffer. */
size_t sat_ring_read_recent(sat_ring_t *,uint32_t before_utc,uint32_t before_seq,
                            uint32_t since_utc,sat_record_t *,size_t capacity);
