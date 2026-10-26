#pragma once
#include "esp_http_server.h"
#include "cJSON.h"
void tracker_backup_init(void);
void tracker_backup_poll(void);
bool tracker_backup_locked(void);
bool tracker_backup_request_allowed(httpd_req_t *req);
void tracker_backup_json(cJSON *root);
void tracker_backup_register(httpd_handle_t server);
