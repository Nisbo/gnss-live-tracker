/* Production HTTP configuration + NVS migration, with a non-transmitting MAC. */
#include "../main/radio_settings.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct {int ns;char key[20];uint8_t data[128];size_t n;} nv_t;
static nv_t nv[40];static char names[8][20];static int spaces;static unsigned reboots,mac_calls;static bool deny,commit_fail,busy;static int64_t clock_us;static int last_dr;
static nv_t *entry(int ns,const char *key,bool create){for(unsigned i=0;i<40;i++)if(nv[i].n&&nv[i].ns==ns&&!strcmp(nv[i].key,key))return &nv[i];if(create)for(unsigned i=0;i<40;i++)if(!nv[i].n){nv[i].ns=ns;strcpy(nv[i].key,key);return &nv[i];}return NULL;}
esp_err_t nvs_open(const char *name,int mode,int *h){for(int i=0;i<spaces;i++)if(!strcmp(name,names[i])){*h=i;return ESP_OK;}if(!mode)return ESP_ERR_NVS_NOT_FOUND;assert(spaces<8);strcpy(names[spaces],name);*h=spaces++;return ESP_OK;}
void nvs_close(int h){(void)h;}esp_err_t nvs_commit(int h){(void)h;return commit_fail?ESP_FAIL:ESP_OK;}
esp_err_t nvs_get_blob(int h,const char *key,void *p,size_t *n){nv_t *v=entry(h,key,false);if(!v)return ESP_ERR_NVS_NOT_FOUND;if(*n<v->n)return ESP_FAIL;memcpy(p,v->data,v->n);*n=v->n;return ESP_OK;}
esp_err_t nvs_set_blob(int h,const char *key,const void *p,size_t n){nv_t *v=entry(h,key,true);assert(v&&n<=128);memcpy(v->data,p,n);v->n=n;return ESP_OK;}
esp_err_t nvs_get_u8(int h,const char *k,uint8_t *v){size_t n=1;return nvs_get_blob(h,k,v,&n);}
esp_err_t nvs_set_u8(int h,const char *k,uint8_t v){return nvs_set_blob(h,k,&v,1);}
esp_err_t nvs_erase_key(int h,const char *key){nv_t *v=entry(h,key,false);if(!v)return ESP_ERR_NVS_NOT_FOUND;v->n=0;return ESP_OK;}
bool tracker_request_authorized(httpd_req_t *r){(void)r;return !deny;}
bool tracker_backup_locked(void){return false;}bool tracker_ota_is_busy(void){return false;}
tracker_credentials_t tracker_credentials_copy(void){return (tracker_credentials_t){0};}
bool lorawan_port_is_tx_pending(void){return busy;}bool lorawan_port_is_join_pending(void){return false;}bool lorawan_port_is_inited(void){return true;}bool lorawan_port_is_joined(void){return true;}
bool lorawan_port_set_adr(bool a){(void)a;mac_calls++;return true;}bool lorawan_port_set_tx_datarate(int8_t d){last_dr=d;mac_calls++;return true;}LoRaMacRegion_t lorawan_port_region(unsigned r){return r==1?LORAMAC_REGION_US915:LORAMAC_REGION_EU868;}int8_t lorawan_port_baseline_power(void){return 14;}unsigned lorawan_port_uplink_dwell(void){return config.dwell;}PhyParam_t RegionGetPhyParam(LoRaMacRegion_t r,GetPhyParams_t *g){(void)r;(void)g;return (PhyParam_t){.Value=config.dwell?2:0};}
bool lorawan_port_reset_adr_power(void){mac_calls++;return true;}
LoRaMacStatus_t LoRaMacMibSetRequestConfirm(MibRequestConfirm_t *m){(void)m;mac_calls++;return LORAMAC_STATUS_OK;}
uint8_t LoRaMacTxBudget(int8_t *dr){*dr=0;return 11;}
int64_t esp_timer_get_time(void){return clock_us;}void esp_restart(void){reboots++;}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *s){(void)s;r->status=status;return ESP_FAIL;}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){snprintf(r->response,sizeof(r->response),"%s",s);return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}
int httpd_req_recv(httpd_req_t *r,char *p,size_t n){if(n>r->content_len-r->received)n=r->content_len-r->received;memcpy(p,r->body+r->received,n);r->received+=n;return n;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *u){(void)h;(void)u;return ESP_OK;}
static httpd_req_t post(const char *body){httpd_req_t r={.content_len=strlen(body),.body=body,.status=200};configure(&r);return r;}
static void defaults(void){position_off=false;config=(tracker_radio_config_t){.version=5,.sf=7,.profile=1,.position_interval_s=300};revision=1;applied=resets=0;reset_pending=apply_failed=false;restart_at=0;}
int main(void){
 int h;nvs_open("tracker_radio",NVS_READWRITE,&h);uint8_t v3[]={3,9,1,2,88,2};nvs_set_blob(h,"config_v3",v3,6);tracker_radio_init();assert(config.version==5&&config.sf==9&&config.adr==1&&config.profile==2&&config.position_interval_s==600&&config.region==0&&config.channel_block==0&&tracker_radio_ready());
 defaults();uint8_t v4[]={4,8,1,1,44,1,1,2,0,0};nvs_set_blob(h,"config_v4",v4,10);tracker_radio_init();assert(config.version==5&&config.region==1&&config.sf==8&&config.channel_block==2&&!config.dwell);nvs_erase_key(h,"config_v4");defaults();tracker_radio_init();
 const char *invalid[]={"{}","{\"sf\":12,\"adr\":false,\"packet_profile\":1,\"region\":1,\"channel_block\":2}","{\"sf\":7,\"adr\":true,\"packet_profile\":1,\"region\":10}","{\"sf\":7,\"adr\":false,\"packet_profile\":0,\"region\":256}","{\"sf\":7,\"adr\":true,\"packet_profile\":1,\"position_interval_s\":301}","{\"sf\":7,\"adr\":true,\"packet_profile\":1,\"region\":0,\"channel_block\":2}","{\"sf\":8,\"adr\":false,\"packet_profile\":0,\"wideband\":true}"};
 for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){tracker_radio_config_t before=config;assert(post(invalid[i]).status==400);assert(!memcmp(&before,&config,sizeof(config)));}
 assert(mac_calls==0);deny=true;assert(post("{}").status==403);deny=false;
 assert(post("{\"sf\":7,\"adr\":false,\"packet_profile\":1,\"position_interval_s\":300}").status==200);tracker_radio_poll();assert(last_dr==5&&tracker_radio_ready());
 unsigned before=mac_calls;httpd_req_t r=post("{\"sf\":10,\"adr\":false,\"packet_profile\":1,\"region\":1,\"channel_block\":2,\"wideband\":false}");assert(r.status==200&&strstr(r.response,"true"));assert(!tracker_radio_ready()&&tracker_radio_restarting()&&config.region==1&&config.channel_block==2);assert(mac_calls==before);assert(post("{}").status==400);tracker_radio_poll();assert(!reboots&&mac_calls==before);clock_us=3000000;busy=true;tracker_radio_poll();assert(!reboots);busy=false;tracker_radio_poll();assert(reboots==1);
 nvs_open("lorawan",NVS_READWRITE,&h);uint8_t sample=1;const char *keys[]={"session","fcnt","fcnt_dn","plan","devnonce","nonce_end"};for(unsigned i=0;i<6;i++)nvs_set_blob(h,keys[i],&sample,1);
 defaults();tracker_radio_init();assert(config.region==1&&config.sf==10&&config.channel_block==2&&tracker_radio_ready()&&!tracker_radio_restarting());for(unsigned i=0;i<4;i++)assert(!entry(h,keys[i],false));assert(entry(h,"devnonce",false)&&entry(h,"nonce_end",false));tracker_radio_poll();assert(last_dr==0);
 r=post("{\"sf\":8,\"adr\":false,\"packet_profile\":2,\"region\":1,\"channel_block\":2,\"wideband\":true}");assert(r.status==200&&!tracker_radio_restarting());tracker_radio_poll();assert(last_dr==4);
 cJSON *root=cJSON_CreateObject();tracker_radio_json(root);cJSON *settings=cJSON_GetObjectItem(root,"settings");assert(cJSON_IsTrue(cJSON_GetObjectItem(settings,"experimental")));assert(cJSON_GetObjectItem(settings,"next_packet_bytes")->valueint==11);cJSON_Delete(root);
 for(unsigned id=2;id<TRACKER_REGION_COUNT;id++){
  defaults();char body[200];snprintf(body,sizeof(body),"{\"sf\":7,\"adr\":false,\"packet_profile\":1,\"region\":%u,\"channel_block\":%u,\"dwell\":%s}",id,tracker_region_fsb(id)?2:0,tracker_region_dwell(id)?"true":"false");
  unsigned calls=mac_calls;assert(post(body).status==200&&!tracker_radio_ready()&&tracker_radio_restarting());assert(mac_calls==calls);defaults();tracker_radio_init();assert(config.region==id&&tracker_radio_ready()&&config.dwell==tracker_region_dwell(id));
 }
 // Position sending persists separately without changing the radio record layout.
 assert(post("{\"sf\":7,\"adr\":false,\"packet_profile\":1,\"position_enabled\":false}").status==200);assert(!tracker_radio_positions_enabled());
 defaults();tracker_radio_init();assert(!tracker_radio_positions_enabled());
 commit_fail=true;assert(post("{\"sf\":7,\"adr\":false,\"packet_profile\":1,\"position_enabled\":true}").status==500);assert(!tracker_radio_positions_enabled());commit_fail=false;
 assert(post("{\"sf\":7,\"adr\":false,\"packet_profile\":1,\"position_enabled\":true}").status==200);assert(tracker_radio_positions_enabled());
 // A failed commit must not apply a new network to the running MAC.
 tracker_radio_config_t previous=config;commit_fail=true;r=post("{\"sf\":7,\"adr\":false,\"packet_profile\":1,\"region\":0,\"channel_block\":0,\"wideband\":false}");assert(r.status==500&&!memcmp(&previous,&config,sizeof(config)));commit_fail=false;
 puts("Production radio settings: v3 migration, validation/auth, region-switch confirmation contract, deferred restart, fresh-session marker, preserved nonces, DR0/DR4 and failed-commit isolation PASS (no hardware)");
}
