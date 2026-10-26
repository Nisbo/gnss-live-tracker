#pragma once
#include <stdint.h>
#include "esp_http_server.h"
void gnss_diagnostics_init(void);
void gnss_diagnostics_line(const char *,int64_t now);
bool gnss_diagnostics_take_probe(void);
void gnss_diagnostics_register(httpd_handle_t);
