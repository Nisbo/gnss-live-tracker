#pragma once
#include "internet_policy.h"
#include "cJSON.h"
#include "esp_http_server.h"
void tracker_internet_init(void);
void tracker_internet_poll(bool connected);
void tracker_internet_json(cJSON *root);
void tracker_internet_register(httpd_handle_t server);
unsigned tracker_internet_position_state(void);
unsigned tracker_internet_pending(int64_t now);
void tracker_internet_event_payload(uint8_t out[8],unsigned state,uint32_t utc);
void tracker_internet_sent(unsigned state,bool success,int64_t now,bool standalone);
