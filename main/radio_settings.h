#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "region_policy.h"
typedef struct {uint8_t version,sf,adr,profile;uint16_t position_interval_s;uint8_t region,channel_block,wideband,rejoin_pending,dwell,reserved;} tracker_radio_config_t;
_Static_assert(sizeof(tracker_radio_config_t)==12,"radio v5 record");
static inline bool tracker_radio_config_valid(tracker_radio_config_t c){
 int dr=tracker_region_dr(c.region,c.sf,c.wideband);
 return c.version==5&&c.adr<=1&&c.profile<=2&&c.wideband<=1&&c.rejoin_pending<=1&&c.dwell<=1&&!c.reserved&&tracker_region_valid(c.region)&&dr>=0&&
  (tracker_region_fsb(c.region)?c.channel_block<=8:c.channel_block==0)&&
  (tracker_region_dwell(c.region)?!c.dwell||dr>=2:c.dwell==0);
}
void tracker_radio_init(void);
tracker_radio_config_t tracker_radio_config(void);
void tracker_radio_poll(void);
void tracker_radio_joined(void);
bool tracker_radio_ready(void);
bool tracker_radio_restarting(void);
void tracker_radio_json(cJSON *lora);
void tracker_radio_register(httpd_handle_t server);
static inline unsigned tracker_radio_packet_bytes(unsigned profile){return profile==0?23:profile==2?38:24;}
