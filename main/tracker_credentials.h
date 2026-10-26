#pragma once
#include "credentials_policy.h"
#include "esp_http_server.h"
#include "cJSON.h"
void tracker_credentials_init(void);
bool tracker_credentials_ready(void);
tracker_credentials_t tracker_credentials_copy(void);
void tracker_credentials_poll(void);
void tracker_credentials_json(cJSON *lora);
void tracker_credentials_register(httpd_handle_t server);
