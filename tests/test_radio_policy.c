#include "radio_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 const unsigned intervals[]={300,600,900,1200,1500,1800,2700,3600};
 for(unsigned i=0;i<sizeof(intervals)/sizeof(*intervals);i++)assert(tracker_radio_interval_valid(intervals[i]));
 const unsigned bad[]={0,60,299,301,3599,3601,65535,0xffffffffu};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!tracker_radio_interval_valid(bad[i]));
 assert(tracker_radio_position_due(-1,300,15000000LL)==15000000LL);
 assert(tracker_radio_position_due(15000000LL,300,0)==315000000LL);
 assert(tracker_radio_position_due(15000000LL,3600,315000000LL)==3615000000LL);
 assert(tracker_radio_position_due(15000000LL,300,3615000000LL)==315000000LL);
 puts("Radio intervals: allowed values, startup, increase/decrease, last-position anchoring: PASS");
}
