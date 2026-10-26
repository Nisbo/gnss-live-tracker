#pragma once
#include "gnss_nmea.h"
#include "esp_http_server.h"
#include "cJSON.h"
bool satellite_log_init(void);
void satellite_log_poll(const tracker_gnss_t *,int64_t);
void satellite_log_json(cJSON *);
void satellite_log_register(httpd_handle_t);
