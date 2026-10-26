#include "adr_history.h"
#include <assert.h>
#include <stdint.h>
static uint64_t clock_ms;
int64_t esp_timer_get_time(void){return clock_ms*1000;}
int main(void){lorawan_adr_event_t rows[8];uint32_t total;assert(lorawan_adr_history(rows,8,&total)==0&&total==0);for(unsigned i=0;i<11;i++){clock_ms=i*1000;lorawan_adr_record((lorawan_adr_event_t){.source=i%2+1,.status=i%2?7:3,.before_dbm=14,.after_dbm=10});}assert(lorawan_adr_history(rows,8,&total)==8&&total==11);assert(rows[0].id==11&&rows[0].uptime_ms==10000&&rows[7].id==4);assert(rows[0].status==3&&rows[1].status==7);assert(lorawan_adr_history(rows,2,&total)==2&&total==11&&rows[1].id==10);return 0;}
