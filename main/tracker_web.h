#pragma once
#include "gnss_nmea.h"
#include "esp_http_server.h"
bool tracker_web_start(void);
void tracker_web_poll(void);
void tracker_web_publish(const tracker_gnss_t *gnss,int64_t now_ms,bool joined);



void tracker_web_lora_timing(int64_t queued_ms,int64_t completed_ms,uint8_t last_port,int64_t next_ms,uint8_t next_port,int64_t join_ms);

void tracker_web_message_queued(uint8_t port,const uint8_t *payload,size_t length,int64_t now_ms);
void tracker_web_message_complete(bool success,int64_t now_ms);

bool tracker_web_backup_valid(const void *,size_t);
