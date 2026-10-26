#include "tracker_internet.h"
#include "tracker_ota.h"
#include "tracker_backup.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static internet_config_t config={.version=1};
static internet_policy_t policy;
static bool wifi,busy,requested,positions_enabled=true;
static uint32_t generation;
static int64_t checked_ms,next_ms,duration_ms;
static unsigned failures;static int http_status,provider;
static int64_t now_ms(void){return esp_timer_get_time()/1000;}
void tracker_internet_init(void){nvs_handle_t h;if(nvs_open("tracker_net",NVS_READONLY,&h)==ESP_OK){internet_config_t c;size_t n=sizeof(c);if(nvs_get_blob(h,"config_v1",&c,&n)==ESP_OK&&n==sizeof(c)&&internet_config_valid(c))config=c;nvs_close(h);}}
/* HTTP runs on a short-lived task. Socket timeouts and the SDK's bounded DNS
   retries cannot stall the GNSS/MAC loop. No TLS session or response body buffer. */
static void probe(void *arg){
 uint32_t start_generation=(uint32_t)(uintptr_t)arg;int64_t started=now_ms();bool online=false;int status=0,which=0;
 const char *urls[]={"http://connectivitycheck.gstatic.com/generate_204","http://cp.cloudflare.com/generate_204"};
 for(int i=0;i<2&&!online;i++){
  portENTER_CRITICAL(&mux);bool valid=wifi&&generation==start_generation;portEXIT_CRITICAL(&mux);if(!valid)break;
  esp_http_client_config_t c={.url=urls[i],.timeout_ms=3000,.buffer_size=512,.buffer_size_tx=512,.disable_auto_redirect=true,.max_redirection_count=0};
  esp_http_client_handle_t client=esp_http_client_init(&c);if(!client)break;
  if(esp_http_client_open(client,0)==ESP_OK){int64_t length=esp_http_client_fetch_headers(client);status=esp_http_client_get_status_code(client);online=length>=0&&status==204;}
  esp_http_client_close(client);esp_http_client_cleanup(client);which=i+1;
 }
 int64_t now=now_ms();portENTER_CRITICAL(&mux);
 if(wifi&&generation==start_generation){checked_ms=now;duration_ms=now-started;http_status=status;provider=which;failures=online?0:failures+1;internet_observe(&policy,online,now);next_ms=now+internet_check_delay(&policy,now);}
 busy=false;portEXIT_CRITICAL(&mux);vTaskDelete(NULL);
}
void tracker_internet_poll(bool connected,bool positions){
 if(tracker_ota_is_busy()||tracker_backup_locked())return;
 int64_t now=now_ms();bool launch=false;uint32_t gen=0;
 portENTER_CRITICAL(&mux);
 positions_enabled=positions;
 if(wifi!=connected){wifi=connected;generation++;next_ms=now;}
 bool active=config.automatic||requested;
 if(!config.events||positions_enabled)policy.reported=policy.state;
 if(active&&!busy&&now>=next_ms){
  requested=false;
  if(!wifi){checked_ms=now;duration_ms=0;provider=http_status=0;failures++;internet_observe(&policy,false,now);next_ms=now+internet_check_delay(&policy,now);}
  else {busy=true;launch=true;gen=generation;next_ms=now+300000;}
 }
 portEXIT_CRITICAL(&mux);
 if(launch&&xTaskCreate(probe,"internet",4096,(void *)(uintptr_t)gen,2,NULL)!=pdPASS){portENTER_CRITICAL(&mux);busy=false;next_ms=now+30000;portEXIT_CRITICAL(&mux);}
}
unsigned tracker_internet_position_state(void){portENTER_CRITICAL(&mux);bool send=config.position&&positions_enabled;unsigned state=policy.state;int64_t age=now_ms()-checked_ms;portEXIT_CRITICAL(&mux);return !send?255:age>600000?0:state;}
unsigned tracker_internet_pending(int64_t now){portENTER_CRITICAL(&mux);unsigned state=internet_event_due(&policy,config.events&&!positions_enabled,now)?policy.state:0;portEXIT_CRITICAL(&mux);return state;}
void tracker_internet_event_payload(uint8_t out[8],unsigned state,uint32_t utc){portENTER_CRITICAL(&mux);internet_event_packet(out,state,wifi,utc,failures);portEXIT_CRITICAL(&mux);}
void tracker_internet_sent(unsigned state,bool success,int64_t now,bool standalone){if(state<1||state>2)return;portENTER_CRITICAL(&mux);if(standalone)policy.last_event_ms=now;if(success)policy.reported=state;portEXIT_CRITICAL(&mux);}
void tracker_internet_json(cJSON *root){
 cJSON *j=cJSON_AddObjectToObject(root,"internet");if(!j)return;int64_t now=now_ms();
 portENTER_CRITICAL(&mux);internet_config_t c=config;internet_policy_t p=policy;bool b=busy,ip=wifi,positions=positions_enabled;int64_t checked=checked_ms,next=next_ms,duration=duration_ms;unsigned failed=failures;int status=http_status,which=provider;portEXIT_CRITICAL(&mux);
 cJSON_AddBoolToObject(j,"automatic",c.automatic);cJSON_AddBoolToObject(j,"position",c.position);cJSON_AddBoolToObject(j,"events",c.events);cJSON_AddBoolToObject(j,"checking",b);cJSON_AddBoolToObject(j,"wifi_ip",ip);cJSON_AddNumberToObject(j,"state",p.state);cJSON_AddNumberToObject(j,"candidate",p.candidate);cJSON_AddNumberToObject(j,"streak",p.streak);cJSON_AddNumberToObject(j,"checked_uptime_ms",checked);cJSON_AddNumberToObject(j,"age_s",checked?(now-checked)/1000:-1);cJSON_AddNumberToObject(j,"duration_ms",duration);cJSON_AddNumberToObject(j,"next_check_uptime_ms",c.automatic?next:0);cJSON_AddNumberToObject(j,"http_status",status);cJSON_AddNumberToObject(j,"failures",failed);cJSON_AddNumberToObject(j,"provider",which);cJSON_AddBoolToObject(j,"event_pending",internet_event_due(&p,c.events&&!positions,now));
}
static esp_err_t post(httpd_req_t *req){
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Reload this page first");
 if(req->content_len<2||req->content_len>256)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid request size");
 char body[257];size_t n=0;while(n<req->content_len){int got=httpd_req_recv(req,body+n,req->content_len-n);if(got<=0)return ESP_FAIL;n+=got;}body[n]=0;cJSON *j=cJSON_Parse(body),*action=cJSON_GetObjectItemCaseSensitive(j,"action");
 bool check=cJSON_IsString(action)&&!strcmp(action->valuestring,"check");esp_err_t err=ESP_OK;
 if(check){portENTER_CRITICAL(&mux);requested=true;if(next_ms>now_ms())next_ms=now_ms();portEXIT_CRITICAL(&mux);}
 else {cJSON *a=cJSON_GetObjectItemCaseSensitive(j,"automatic"),*p=cJSON_GetObjectItemCaseSensitive(j,"position"),*e=cJSON_GetObjectItemCaseSensitive(j,"events");internet_config_t c={1,cJSON_IsTrue(a),cJSON_IsTrue(p),cJSON_IsTrue(e)};
  if(!cJSON_IsBool(a)||!cJSON_IsBool(p)||!cJSON_IsBool(e)||!internet_config_valid(c)||(c.events&&positions_enabled)||(c.position&&!positions_enabled)){cJSON_Delete(j);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid Internet settings");}
  nvs_handle_t h;err=nvs_open("tracker_net",NVS_READWRITE,&h);if(err==ESP_OK){err=nvs_set_blob(h,"config_v1",&c,sizeof(c));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}if(err==ESP_OK){portENTER_CRITICAL(&mux);bool enabled=!config.automatic&&c.automatic;config=c;if(enabled)next_ms=now_ms();if(!c.events)policy.reported=policy.state;portEXIT_CRITICAL(&mux);}
 }
 cJSON_Delete(j);if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save Internet settings");httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");return httpd_resp_sendstr(req,"{\"ok\":true}");
}
void tracker_internet_register(httpd_handle_t server){const httpd_uri_t uri={.uri="/api/internet",.method=HTTP_POST,.handler=post};ESP_ERROR_CHECK(httpd_register_uri_handler(server,&uri));}

bool tracker_internet_events_enabled(void){portENTER_CRITICAL(&mux);bool enabled=config.events;portEXIT_CRITICAL(&mux);return enabled;}
