#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#define TRACKER_MESSAGE_MAX 51
typedef struct {bool valid,success;uint8_t fport,length;uint8_t payload[TRACKER_MESSAGE_MAX];int64_t queued_ms,completed_ms;uint32_t counter;} tracker_message_t;
typedef struct {tracker_message_t pending,last;} tracker_message_log_t;
static inline bool tracker_message_queue(tracker_message_log_t *log,uint8_t fport,const uint8_t *payload,size_t length,int64_t now){
 if(length>TRACKER_MESSAGE_MAX||(length&&!payload)||(!length&&fport!=0))return false;
 log->pending=(tracker_message_t){.valid=true,.fport=fport,.length=length,.queued_ms=now};if(length)memcpy(log->pending.payload,payload,length);return true;
}
static inline void tracker_message_complete(tracker_message_log_t *log,bool success,int64_t now,uint32_t counter){
 if(!log->pending.valid)return;
 log->last=log->pending;log->last.success=success;log->last.completed_ms=now;log->last.counter=counter;log->pending.valid=false;
}
