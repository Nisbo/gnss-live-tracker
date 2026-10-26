#pragma once
#include <stdbool.h>
#include "esp_http_server.h"
#include "cJSON.h"
void tracker_ota_register(httpd_handle_t server);
void tracker_ota_poll(void);
bool tracker_ota_is_busy(void);
void tracker_ota_json(cJSON *root);
bool tracker_request_authorized(httpd_req_t *req);
