#include "lora_messages.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 tracker_message_log_t log={0};uint8_t position[39]={2,2},satellites[11]={1,0,1,1,1,1,1,0xe3,0,23,29};
 tracker_message_complete(&log,true,1,1);assert(!log.last.valid);
 assert(tracker_message_queue(&log,10,position,39,10));position[1]=0;assert(log.pending.payload[1]==2);assert(!log.last.valid);
 tracker_message_complete(&log,true,20,42);assert(log.last.valid&&log.last.success&&log.last.counter==42&&log.last.fport==10&&log.last.completed_ms==20&&!log.pending.valid);
 assert(tracker_message_queue(&log,11,satellites,11,25));assert(log.last.fport==10);satellites[10]=0;assert(log.pending.payload[10]==29);
 tracker_message_complete(&log,false,30,43);assert(log.last.fport==11&&!log.last.success&&log.last.payload[10]==29&&log.last.counter==43);
 assert(!tracker_message_queue(&log,10,position,52,35));assert(!tracker_message_queue(&log,10,NULL,3,35));assert(!tracker_message_queue(&log,10,position,0,35));assert(log.last.counter==43);
 assert(tracker_message_queue(&log,0,NULL,0,40));tracker_message_complete(&log,true,50,44);assert(log.last.valid&&log.last.fport==0&&log.last.length==0&&log.last.counter==44);
 puts("LoRa message snapshots: copied payload, queued/completed separation, failed attempts, invalid lengths and no phantom completion passed");
}
