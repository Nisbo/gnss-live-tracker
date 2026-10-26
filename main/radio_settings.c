#include "radio_settings.h"
#include "radio_policy.h"
#include "lorawan_port.h"
#include "tracker_ota.h"
#include "tracker_backup.h"
#include "tracker_credentials.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static tracker_radio_config_t config={.version=5,.sf=7,.profile=1,.position_interval_s=300};
static unsigned revision=1,applied,resets;
static bool apply_failed,reset_pending,position_off;
bool tracker_radio_positions_enabled(void){portENTER_CRITICAL(&mux);bool enabled=!position_off;portEXIT_CRITICAL(&mux);return enabled;}
static int64_t restart_at;
tracker_radio_config_t tracker_radio_config(void){portENTER_CRITICAL(&mux);tracker_radio_config_t c=config;portEXIT_CRITICAL(&mux);return c;}
bool tracker_radio_ready(void){return !tracker_radio_config().rejoin_pending;}
bool tracker_radio_restarting(void){portENTER_CRITICAL(&mux);bool b=restart_at||config.rejoin_pending;portEXIT_CRITICAL(&mux);return b;}
static esp_err_t store_full(const tracker_radio_config_t *c,int off){nvs_handle_t h;esp_err_t e=nvs_open("tracker_radio",NVS_READWRITE,&h);if(e==ESP_OK){e=nvs_set_blob(h,"config_v5",c,sizeof(*c));if(e==ESP_OK&&off>=0)e=nvs_set_u8(h,"position_off",off);if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);}return e;}
static esp_err_t store(const tracker_radio_config_t *c){return store_full(c,-1);}
static esp_err_t clear_session(void){nvs_handle_t h;esp_err_t e=nvs_open("lorawan",NVS_READWRITE,&h);if(e!=ESP_OK)return e;const char *keys[]={"session","fcnt","fcnt_dn","plan","context"};for(unsigned i=0;i<5;i++){e=nvs_erase_key(h,keys[i]);if(e!=ESP_OK&&e!=ESP_ERR_NVS_NOT_FOUND){nvs_close(h);return e;}}e=nvs_commit(h);nvs_close(h);return e;}
void tracker_radio_init(void){
 nvs_handle_t h;if(nvs_open("tracker_radio",NVS_READONLY,&h)!=ESP_OK)return;
 uint8_t off=0;if(nvs_get_u8(h,"position_off",&off)==ESP_OK&&off<=1)position_off=off;
 tracker_radio_config_t c={0};size_t n=sizeof(c);esp_err_t e=nvs_get_blob(h,"config_v5",&c,&n);
 if(e==ESP_OK&&n==sizeof(c)&&tracker_radio_config_valid(c)&&tracker_radio_interval_valid(c.position_interval_s))config=c;
 else {uint8_t v4[10]={0};n=sizeof(v4);e=nvs_get_blob(h,"config_v4",v4,&n);
 if(e==ESP_OK&&n==10&&v4[0]==4){memcpy(&c,v4,10);c.version=5;c.dwell=c.reserved=0;if(tracker_radio_config_valid(c)&&tracker_radio_interval_valid(c.position_interval_s)){config=c;goto loaded;}}
 uint8_t old[6]={0};n=6;e=nvs_get_blob(h,"config_v3",old,&n);bool v3=e==ESP_OK&&n==6&&old[0]==3&&old[3]<=2;
  if(!v3){n=6;e=nvs_get_blob(h,"config_v2",old,&n);}uint16_t interval=0;memcpy(&interval,old+4,2);
  if(e==ESP_OK&&n==6&&(v3||old[0]==2)&&old[1]>=7&&old[1]<=12&&old[2]<=1&&tracker_radio_interval_valid(interval))config=(tracker_radio_config_t){.version=5,.sf=old[1],.adr=old[2],.profile=v3?old[3]:1,.position_interval_s=interval};
  else {n=4;if(nvs_get_blob(h,"config_v1",old,&n)==ESP_OK&&n==4&&old[0]==1&&old[1]>=7&&old[1]<=12&&old[2]<=1){config.sf=old[1];config.adr=old[2];}}
 }
loaded:
 nvs_close(h);
 if(config.rejoin_pending&&clear_session()==ESP_OK){c=config;c.rejoin_pending=0;if(store(&c)==ESP_OK)config=c;}
}
/* Reapply the selected starting rate after the regional join procedure. */
void tracker_radio_joined(void){portENTER_CRITICAL(&mux);revision++;portEXIT_CRITICAL(&mux);}
/* All MAC calls and restarts run on the main task. */
void tracker_radio_poll(void){
 portENTER_CRITICAL(&mux);unsigned r=revision,a=applied;tracker_radio_config_t c=config;int64_t at=restart_at;portEXIT_CRITICAL(&mux);
 if(at){if(esp_timer_get_time()/1000>=at&&!tracker_backup_locked()&&!tracker_ota_is_busy()&&!lorawan_port_is_tx_pending()&&!lorawan_port_is_join_pending())esp_restart();return;}
 if(c.rejoin_pending||r==a||!lorawan_port_is_inited()||lorawan_port_is_tx_pending()||lorawan_port_is_join_pending())return;
 int dr=tracker_region_dr(c.region,c.sf,c.wideband);
 bool ok=lorawan_port_set_adr(false)&&lorawan_port_set_tx_datarate(dr);
 if(ok){MibRequestConfirm_t mib={.Type=MIB_CHANNELS_DATARATE};GetPhyParams_t min={.Attribute=PHY_MIN_TX_DR,.UplinkDwellTime=lorawan_port_uplink_dwell()};dr=MAX(dr,(int)RegionGetPhyParam(lorawan_port_region(c.region),&min).Value);mib.Param.ChannelsDatarate=dr;ok=LoRaMacMibSetRequestConfirm(&mib)==LORAMAC_STATUS_OK;}
 if(ok&&c.adr)ok=lorawan_port_set_tx_datarate(-1)&&lorawan_port_set_adr(true);
 portENTER_CRITICAL(&mux);bool reset=reset_pending;portEXIT_CRITICAL(&mux);if(ok&&reset)ok=lorawan_port_reset_adr_power();
 portENTER_CRITICAL(&mux);if(ok&&reset){reset_pending=false;resets++;}apply_failed=!ok;if(ok)applied=r;portEXIT_CRITICAL(&mux);
}
void tracker_radio_json(cJSON *lora){
 tracker_radio_config_t c=tracker_radio_config();cJSON *j=cJSON_AddObjectToObject(lora,"settings");if(!j)return;
 cJSON_AddNumberToObject(j,"sf",c.sf);cJSON_AddBoolToObject(j,"adr",c.adr);cJSON_AddNumberToObject(j,"packet_profile",c.profile);cJSON_AddNumberToObject(j,"region",c.region);cJSON_AddStringToObject(j,"region_name",tracker_region_name(c.region));cJSON_AddNumberToObject(j,"channel_block",c.channel_block);cJSON_AddNumberToObject(j,"bandwidth_khz",c.wideband?500:125);cJSON_AddBoolToObject(j,"wideband",c.wideband);cJSON_AddBoolToObject(j,"dwell",c.dwell);cJSON_AddBoolToObject(j,"experimental",c.region!=0);
 cJSON_AddBoolToObject(j,"position_enabled",tracker_radio_positions_enabled());
 cJSON_AddNumberToObject(j,"position_interval_s",c.position_interval_s);cJSON_AddNumberToObject(j,"position_bytes",tracker_radio_packet_bytes(c.profile));
 portENTER_CRITICAL(&mux);bool pending=revision!=applied,failed=apply_failed,reset=reset_pending;unsigned count=resets;portEXIT_CRITICAL(&mux);
 cJSON_AddBoolToObject(j,"apply_pending",pending);cJSON_AddBoolToObject(j,"apply_failed",failed);cJSON_AddBoolToObject(j,"reset_pending",reset);cJSON_AddBoolToObject(j,"restart_pending",tracker_radio_restarting());cJSON_AddNumberToObject(j,"reset_count",count);cJSON_AddNumberToObject(j,"baseline_dbm",lorawan_port_baseline_power());cJSON_AddNumberToObject(j,"uplink_dwell",lorawan_port_uplink_dwell());
 int8_t dr=0;unsigned budget=LoRaMacTxBudget(&dr),wanted=tracker_radio_packet_bytes(c.profile);cJSON_AddNumberToObject(j,"next_payload_budget",budget);cJSON_AddNumberToObject(j,"next_packet_bytes",tracker_packet_size(wanted,budget));cJSON_AddNumberToObject(j,"next_packet_dr",dr);
}
static esp_err_t configure(httpd_req_t *req){
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Request token required");
 if(tracker_radio_restarting()||tracker_credentials_copy().reset_session||tracker_ota_is_busy())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Restart/update pending");
 if(req->content_len<2||req->content_len>256)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid size");
 char body[257];int n=0;while(n<req->content_len){int got=httpd_req_recv(req,body+n,req->content_len-n);if(got<=0)return ESP_FAIL;n+=got;}body[n]=0;
 cJSON *j=cJSON_Parse(body),*sf=cJSON_GetObjectItemCaseSensitive(j,"sf"),*adr=cJSON_GetObjectItemCaseSensitive(j,"adr"),*profile=cJSON_GetObjectItemCaseSensitive(j,"packet_profile");
 bool valid=cJSON_IsObject(j)&&cJSON_IsNumber(sf)&&sf->valuedouble==sf->valueint&&sf->valueint>=7&&sf->valueint<=12&&cJSON_IsBool(adr)&&cJSON_IsNumber(profile)&&profile->valuedouble==profile->valueint&&profile->valueint>=0&&profile->valueint<=2;
 tracker_radio_config_t c=tracker_radio_config();c.version=5;c.sf=valid?sf->valueint:7;c.adr=cJSON_IsTrue(adr);c.profile=valid?profile->valueint:1;
 const char *names[]={"position_interval_s","region","channel_block"};unsigned values[]={c.position_interval_s,c.region,c.channel_block};
 for(unsigned i=0;i<3;i++){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,names[i]);if(v){if(!cJSON_IsNumber(v)||v->valuedouble!=v->valueint||v->valuedouble<0||v->valuedouble>(i==0?3600:i==1?TRACKER_REGION_COUNT-1:8))valid=false;else values[i]=v->valueint;}}
 c.position_interval_s=values[0];c.region=values[1];c.channel_block=values[2];cJSON *wide=cJSON_GetObjectItemCaseSensitive(j,"wideband");if(wide){if(cJSON_IsBool(wide))c.wideband=cJSON_IsTrue(wide);else if(cJSON_IsNumber(wide)&&wide->valueint>=0&&wide->valueint<=1&&wide->valueint==wide->valuedouble)c.wideband=wide->valueint;else valid=false;}int off=-1;cJSON *positions=cJSON_GetObjectItemCaseSensitive(j,"position_enabled");if(positions){if(!cJSON_IsBool(positions))valid=false;else off=!cJSON_IsTrue(positions);}
 cJSON *dwell=cJSON_GetObjectItemCaseSensitive(j,"dwell");if(dwell){if(!cJSON_IsBool(dwell))valid=false;else c.dwell=cJSON_IsTrue(dwell);}cJSON_Delete(j);
 if(!valid||!tracker_radio_config_valid(c)||!tracker_radio_interval_valid(c.position_interval_s))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid region, channel block, data rate, profile or interval");
 tracker_radio_config_t old=tracker_radio_config();bool network=c.region!=old.region||c.channel_block!=old.channel_block||c.dwell!=old.dwell;c.rejoin_pending=network;
 esp_err_t e=store_full(&c,off);if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save radio settings");
 portENTER_CRITICAL(&mux);bool change=c.sf!=config.sf||c.adr!=config.adr||c.wideband!=config.wideband;config=c;if(off>=0)position_off=off;if(change)revision++;if(network)restart_at=esp_timer_get_time()/1000+2000;portEXIT_CRITICAL(&mux);
 httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,network?"{\"ok\":true,\"restarting\":true}":"{\"ok\":true,\"restarting\":false}");
}
static esp_err_t reset_radio(httpd_req_t *req){
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Request token required");
 if(req->content_len!=0||tracker_radio_restarting()||tracker_credentials_copy().reset_session||tracker_ota_is_busy())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid reset or restart pending");
 portENTER_CRITICAL(&mux);if(!reset_pending){reset_pending=true;revision++;}portEXIT_CRITICAL(&mux);
 httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true,\"pending\":true}");
}
void tracker_radio_register(httpd_handle_t server){const httpd_uri_t routes[]={{.uri="/api/lora",.method=HTTP_POST,.handler=configure},{.uri="/api/lora/reset",.method=HTTP_POST,.handler=reset_radio}};for(unsigned i=0;i<2;i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));}
