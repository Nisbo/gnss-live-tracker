#pragma once
#include "esp_http_server.h"
#include "cJSON.h"
bool import_log_init(void);
void import_log_json(cJSON *);
void import_log_register(httpd_handle_t);
