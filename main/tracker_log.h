#pragma once
#include "gnss_nmea.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "track_ring.h"
bool tracker_log_init(void);
void tracker_log_poll(const tracker_gnss_t *,int64_t now);
void tracker_log_json(cJSON *root);
void tracker_log_register(httpd_handle_t);

bool tracker_log_configure(int enabled,int interval_s);

bool tracker_log_merge_records(track_record_t *,size_t,unsigned *added,unsigned *duplicates);
