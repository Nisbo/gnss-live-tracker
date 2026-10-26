#include "radio_settings.h"
#include "tracker_credentials.h"
#include <stdio.h>
#include "tracker_ota.h"
#include "tracker_backup.h"
#include "lorawan_port.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static tracker_credentials_t config;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t write_mutex;
static bool configured;
static int64_t restart_at;
tracker_credentials_t tracker_credentials_copy(void){portENTER_CRITICAL(&mux);tracker_credentials_t c=config;portEXIT_CRITICAL(&mux);return c;}
bool tracker_credentials_ready(void){portENTER_CRITICAL(&mux);bool ready=configured&&!config.reset_session;portEXIT_CRITICAL(&mux);return ready;}
static esp_err_t store(const tracker_credentials_t *c){nvs_handle_t h;esp_err_t e=nvs_open("tracker_otaa",NVS_READWRITE,&h);if(e==ESP_OK){e=nvs_set_blob(h,"config_v1",c,sizeof(*c));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);}return e;}
/* Invalidate only session state, never WLAN, logs or nonce-related keys. */
static esp_err_t clear_old_session(void){nvs_handle_t h;esp_err_t e=nvs_open("lorawan",NVS_READWRITE,&h);if(e!=ESP_OK)return e;const char *keys[]={"session","fcnt","fcnt_dn","plan","context"};for(unsigned i=0;i<5;i++){e=nvs_erase_key(h,keys[i]);if(e!=ESP_OK&&e!=ESP_ERR_NVS_NOT_FOUND){nvs_close(h);return e;}}e=nvs_commit(h);nvs_close(h);return e;}
void tracker_credentials_init(void){
 write_mutex=xSemaphoreCreateMutex();nvs_handle_t h;if(nvs_open("tracker_otaa",NVS_READONLY,&h)!=ESP_OK)return;tracker_credentials_t c={0};size_t n=sizeof(c);esp_err_t e=nvs_get_blob(h,"config_v1",&c,&n);nvs_close(h);
 if(e==ESP_OK&&n==sizeof(c)&&c.version==1&&c.reset_session<=1){config=c;configured=true;if(c.reset_session&&clear_old_session()==ESP_OK){c.reset_session=0;if(store(&c)==ESP_OK)config=c;}}
 memset(&c,0,sizeof(c));
}
void tracker_credentials_poll(void){portENTER_CRITICAL(&mux);int64_t at=restart_at;portEXIT_CRITICAL(&mux);if(at&&esp_timer_get_time()/1000>=at&&!tracker_backup_locked()&&!tracker_ota_is_busy()&&!lorawan_port_is_tx_pending()&&!lorawan_port_is_join_pending())esp_restart();}
void tracker_credentials_json(cJSON *lora){tracker_credentials_t c=tracker_credentials_copy();cJSON *j=cJSON_AddObjectToObject(lora,"credentials");if(!j)return;bool ready=tracker_credentials_ready();cJSON_AddBoolToObject(j,"configured",c.version==1);cJSON_AddBoolToObject(j,"ready",ready);cJSON_AddBoolToObject(j,"restart_pending",c.reset_session);char hex[17];credential_hex(c.dev_eui,8,hex);cJSON_AddStringToObject(j,"dev_eui",c.version==1?hex:"");credential_hex(c.join_eui,8,hex);cJSON_AddStringToObject(j,"join_eui",c.version==1?hex:"");cJSON_AddBoolToObject(j,"app_key_set",c.version==1);memset(&c,0,sizeof(c));}
static esp_err_t reveal(httpd_req_t *req){
 if(tracker_radio_restarting())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Region restart pending");
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Request token required");
 tracker_credentials_t c=tracker_credentials_copy();if(c.version!=1)return httpd_resp_send_err(req,HTTPD_404_NOT_FOUND,"No credentials configured");char hex[33];credential_hex(c.app_key,16,hex);char body[48];snprintf(body,sizeof(body),"{\"app_key\":\"%s\"}",hex);httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");esp_err_t e=httpd_resp_sendstr(req,body);memset(&c,0,sizeof(c));memset(hex,0,sizeof(hex));memset(body,0,sizeof(body));return e;
}
static esp_err_t save(httpd_req_t *req){
 if(tracker_ota_is_busy()){httpd_resp_set_status(req,"409 Conflict");return httpd_resp_sendstr(req,"Wait until firmware update is complete");}
 if(tracker_radio_restarting())return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Region restart pending");
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Request token required");
 if(req->content_len<2||req->content_len>384)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid credential size");
 char body[385];int n=0;while(n<req->content_len){int got=httpd_req_recv(req,body+n,req->content_len-n);if(got<=0){memset(body,0,sizeof(body));return ESP_FAIL;}n+=got;}body[n]=0;cJSON *j=cJSON_Parse(body);memset(body,0,sizeof(body));
 if(!write_mutex||xSemaphoreTake(write_mutex,pdMS_TO_TICKS(100))!=pdTRUE){cJSON_Delete(j);httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Credential save busy");}
 tracker_credentials_t old=tracker_credentials_copy(),c=old;const cJSON *dev=cJSON_GetObjectItemCaseSensitive(j,"dev_eui"),*join=cJSON_GetObjectItemCaseSensitive(j,"join_eui"),*key=cJSON_GetObjectItemCaseSensitive(j,"app_key");
 bool valid=cJSON_IsObject(j)&&cJSON_IsString(dev)&&credential_parse(dev->valuestring,c.dev_eui,8)&&cJSON_IsString(join)&&credential_parse(join->valuestring,c.join_eui,8)&&(!key||cJSON_IsString(key));
 if(cJSON_IsString(key)&&key->valuestring[0])valid=valid&&credential_parse(key->valuestring,c.app_key,16);else valid=valid&&old.version==1;
 if(cJSON_IsString(key))memset(key->valuestring,0,strlen(key->valuestring));
 cJSON_Delete(j);
 if(!valid){memset(&old,0,sizeof(old));memset(&c,0,sizeof(c));xSemaphoreGive(write_mutex);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"DevEUI and JoinEUI need 16 hex digits; AppKey needs 32 (blank keeps saved key)");}
 bool change=old.version!=1||!credential_equal(&old,&c);c.version=1;if(change)c.reset_session=1;esp_err_t e=change?store(&c):ESP_OK;
 if(e==ESP_OK&&change){portENTER_CRITICAL(&mux);config=c;configured=true;restart_at=esp_timer_get_time()/1000+2000;portEXIT_CRITICAL(&mux);}
 bool restarting=c.reset_session;memset(&old,0,sizeof(old));memset(&c,0,sizeof(c));xSemaphoreGive(write_mutex);
 if(e!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save credentials");
 httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,restarting?"{\"ok\":true,\"restarting\":true}":"{\"ok\":true,\"restarting\":false}");
}
void tracker_credentials_register(httpd_handle_t server){const httpd_uri_t routes[]={{.uri="/api/lora/credentials",.method=HTTP_GET,.handler=reveal},{.uri="/api/lora/credentials",.method=HTTP_POST,.handler=save}};for(unsigned i=0;i<2;i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));}
