#pragma once
#include "gnss_nmea.h"
#include "esp_http_server.h"
#include "cJSON.h"
void tracker_display_init(void);
void tracker_display_update(const tracker_gnss_t *gnss, int64_t now_ms, bool joined);

void tracker_display_poll(int64_t now_ms);
void tracker_display_wake(void);
void tracker_display_register(httpd_handle_t server);
void tracker_display_json(cJSON *root);

/* Button cycling eligibility: false while dark or showing the clock saver. */
bool tracker_display_is_on(void);
void tracker_display_button(unsigned event);
bool tracker_display_refresh_requested(void);
void tracker_display_lora_timing(int64_t last_ms,int64_t next_ms,uint8_t port);

void tracker_display_services(const cJSON *root);
