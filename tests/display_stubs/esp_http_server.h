#pragma once
#include "../backup_stubs/esp_http_server.h"
esp_err_t httpd_resp_send(httpd_req_t *,const char *,size_t);
