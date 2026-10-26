#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define TRACKER_MAX_SATS 96
#define POSITION_BYTES 39
#define POSITION_COMPACT_BYTES 24

#define SATS_PER_PACKET 11
/* Systems: 1 GPS, 2 GLONASS, 3 Galileo, 4 BeiDou, 5 QZSS, 6 SBAS, 7 NavIC. */
typedef struct {
    uint8_t system, cn0, used_sources, elevation;
    uint16_t id, azimuth;
    int64_t seen_ms, used_ms, cn0_epoch;
} tracker_sat_t;
typedef struct {
    double latitude, longitude, altitude, hdop, speed_mps, course;
    double sigma_lat,sigma_lon,sigma_alt;
    int64_t gst_ms,gga_clock_ms;
    int64_t gga_ms, rmc_ms, last_fix_ms, nmea_ms, gsa_ms;
    int64_t utc_epoch_ms,utc_received_ms;
    bool utc_confirmed;
    bool configuration_known;
    uint32_t configuration_mask;
    int64_t gsa_system_ms[8], gsv_system_ms[8];
    bool gga_fix, rmc_valid, overflow;
    uint8_t quality, used;
    uint32_t sentences, checksum_errors;
    tracker_sat_t satellites[TRACKER_MAX_SATS];
} tracker_gnss_t;
#define SAT_VISIBLE 1
#define SAT_USED 2
/* Snapshot timestamps were only booleans. Keep the live parser timestamps above. */
typedef struct {uint16_t id,azimuth;uint8_t system,cn0,elevation,flags;} tracker_sat_view_t;
_Static_assert(sizeof(tracker_sat_view_t)==8,"Compact snapshot must stay 8 bytes");
_Static_assert(sizeof(tracker_sat_t)==32,"Angles must fit the existing live record padding");
typedef struct {
    tracker_sat_view_t satellites[TRACKER_MAX_SATS];
    uint8_t count;
    uint16_t sequence;
    uint8_t usage_known_mask;
} tracker_sat_snapshot_t;
void tracker_gnss_init(tracker_gnss_t *g);
bool tracker_nmea_line(tracker_gnss_t *g, const char *line, int64_t now_ms);
bool tracker_gnss_has_fix(const tracker_gnss_t *g, int64_t now_ms);
void tracker_position_payload(const tracker_gnss_t *g, int64_t now_ms,
                              uint16_t sequence, uint8_t out[POSITION_BYTES]);
void tracker_sat_snapshot(const tracker_gnss_t *g, int64_t now_ms,
                          uint16_t sequence, tracker_sat_snapshot_t *out);
size_t tracker_sat_packet(const tracker_sat_snapshot_t *s, uint8_t chunk, uint8_t out[51]);

/* UTC of the actual GGA measurement, independent of browser/wall clock. */
int64_t tracker_gnss_position_utc(const tracker_gnss_t *,int64_t now_ms);

void tracker_position_compact(const tracker_gnss_t *,int64_t now_ms,uint16_t sequence,uint8_t out[POSITION_COMPACT_BYTES]);

/* Profiles: 0 basis (23 bytes), 1 standard (24), 2 system counts (38). */
size_t tracker_position_packet(const tracker_gnss_t *,int64_t,uint16_t,unsigned,uint8_t out[38]);

size_t tracker_position_small(const tracker_gnss_t *,int64_t,uint16_t,unsigned,uint8_t out[11]);
