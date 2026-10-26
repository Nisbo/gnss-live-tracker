#pragma once
#include <stddef.h>
#include "gnss_nmea.h"
/* All cache acquire/replace/release operations require the caller's mutex.
 * A response owns a reference until its last send, including failed sends. */
typedef struct status_blob {size_t length;unsigned refs;char data[];} status_blob_t;
typedef struct {status_blob_t *current;} status_cache_t;
status_blob_t *status_build(const char *metadata,const uint8_t position[POSITION_BYTES],const tracker_sat_snapshot_t *satellites);
status_blob_t *status_acquire(status_cache_t *cache);
void status_release(status_blob_t *blob);
void status_replace(status_cache_t *cache,status_blob_t *next);
