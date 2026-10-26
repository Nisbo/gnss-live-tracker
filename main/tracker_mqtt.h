#pragma once
#include "gnss_nmea.h"
#include "esp_http_server.h"
#include "cJSON.h"
bool tracker_mqtt_init(void);
void tracker_mqtt_register(httpd_handle_t server);
void tracker_mqtt_json(cJSON *root);
void tracker_mqtt_snapshot(const tracker_gnss_t *,int64_t now,bool joined);

bool tracker_mqtt_backup_valid(const void *,size_t);
