#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tracker_internet.c"
static int64_t clock_ms=1000;
static void (*task_fn)(void *);static void *task_arg;
static unsigned allocated,cleaned,workers,deleted;static bool deny_auth,drop_link,fail_task,has_saved;static int codes[2]={204,204},client_index;static internet_config_t saved_config;
int64_t esp_timer_get_time(void){return clock_ms*1000;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *out){(void)name;(void)priority;(void)out;assert(stack==4096);if(fail_task)return 0;assert(!task_fn);task_fn=fn;task_arg=arg;workers++;return pdPASS;}
void vTaskDelete(void *p){(void)p;deleted++;}void vTaskDelay(unsigned n){clock_ms+=n;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){assert(c->timeout_ms==3000&&c->buffer_size==512&&c->disable_auto_redirect);client_index=strstr(c->url,"cloudflare")?1:0;allocated++;return (void *)1;}
esp_err_t esp_http_client_open(void *p,int n){(void)p;(void)n;if(drop_link){drop_link=false;tracker_internet_poll(false,positions_enabled);}return ESP_OK;}
int64_t esp_http_client_fetch_headers(void *p){(void)p;clock_ms+=20;return 0;}int esp_http_client_get_status_code(void *p){(void)p;return codes[client_index];}
esp_err_t esp_http_client_close(void *p){(void)p;return ESP_OK;}esp_err_t esp_http_client_cleanup(void *p){(void)p;cleaned++;return ESP_OK;}
esp_err_t nvs_open(const char *s,int mode,int *h){(void)mode;assert(!strcmp(s,"tracker_net"));*h=1;return ESP_OK;}void nvs_close(int h){(void)h;}esp_err_t nvs_get_blob(int h,const char *k,void *b,size_t *n){(void)h;assert(!strcmp(k,"config_v1"));if(!has_saved)return ESP_ERR_NVS_NOT_FOUND;assert(*n>=sizeof(saved_config));memcpy(b,&saved_config,sizeof(saved_config));*n=sizeof(saved_config);return ESP_OK;}
esp_err_t nvs_set_blob(int h,const char *k,const void *b,size_t n){(void)h;assert(!strcmp(k,"config_v1")&&n==sizeof(saved_config));memcpy(&saved_config,b,n);has_saved=true;return ESP_OK;}esp_err_t nvs_commit(int h){(void)h;return ESP_OK;}
bool tracker_ota_is_busy(void){return false;}bool tracker_backup_locked(void){return false;}
bool tracker_request_authorized(httpd_req_t *r){(void)r;return !deny_auth;}
int httpd_req_recv(httpd_req_t *r,char *b,size_t n){if(n>r->content_len-r->received)n=r->content_len-r->received;memcpy(b,r->body+r->received,n);r->received+=n;return n;}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int code,const char *s){r->status=code;(void)s;return ESP_FAIL;}esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){snprintf(r->response,sizeof(r->response),"%s",s);return ESP_OK;}esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;(void)k;(void)v;return ESP_OK;}esp_err_t httpd_register_uri_handler(void *s,const httpd_uri_t *u){(void)s;(void)u;return ESP_OK;}
static int request(const char *body){httpd_req_t r={.body=body,.content_len=strlen(body),.status=200};post(&r);return r.status;}
static void run_task(void){assert(task_fn);void (*fn)(void *)=task_fn;void *arg=task_arg;task_fn=NULL;fn(arg);assert(!busy);}
int main(void){
 tracker_internet_init();positions_enabled=true;tracker_internet_poll(true,positions_enabled);assert(!workers&&!has_saved&&tracker_internet_position_state()==255);
 deny_auth=true;assert(request("{\"action\":\"check\"}")==403);deny_auth=false;
 assert(request("{\"automatic\":false,\"position\":true,\"events\":false}")==400);
 assert(request("{\"automatic\":true,\"position\":true,\"events\":true}")==400);
 assert(request("{\"automatic\":true,\"position\":true,\"events\":false}")==200);
 tracker_internet_poll(true,positions_enabled);assert(busy&&workers==1&&allocated==0);run_task();assert(policy.state==2&&tracker_internet_position_state()==2&&!tracker_internet_pending(clock_ms));
 // Standalone alerts are available only with position reports disabled.
 tracker_internet_poll(true,false);assert(tracker_internet_position_state()==255);
 assert(request("{\"automatic\":true,\"position\":false,\"events\":true}")==200);
 codes[0]=codes[1]=302;clock_ms=next_ms;tracker_internet_poll(true,false);run_task();assert(policy.state==2);
 int64_t first=clock_ms;assert(next_ms-first==60000);
 // A manual retry before the minute cannot prematurely confirm an outage.
 clock_ms+=10000;assert(request("{\"action\":\"check\"}")==200);tracker_internet_poll(true,false);run_task();assert(policy.state==2);
 clock_ms=next_ms;tracker_internet_poll(true,false);run_task();assert(policy.state==1&&tracker_internet_pending(clock_ms)==1);
 // Recovery uses the same one-minute confirmation and cancels an obsolete alert.
 codes[1]=204;clock_ms=next_ms;tracker_internet_poll(true,false);run_task();assert(policy.state==1);
 clock_ms=next_ms;tracker_internet_poll(true,false);run_task();assert(policy.state==2&&!tracker_internet_pending(clock_ms));
 // No-IP outages follow the same two-observation policy.
 clock_ms+=1000;tracker_internet_poll(false,false);assert(policy.state==2);clock_ms=next_ms;tracker_internet_poll(false,false);assert(policy.state==1);
 uint8_t event[8];tracker_internet_event_payload(event,1,1767225600);assert(event[0]==1&&event[1]==1&&!event[2]&&event[7]>=2);
 tracker_internet_sent(1,false,clock_ms,true);assert(!tracker_internet_pending(clock_ms));clock_ms+=300000;assert(tracker_internet_pending(clock_ms)==1);tracker_internet_sent(1,true,clock_ms,true);assert(!tracker_internet_pending(clock_ms));
 // Stale results from an old Wi-Fi connection must not confirm recovery.
 tracker_internet_poll(true,positions_enabled);assert(task_fn);unsigned before=policy.state;drop_link=true;run_task();assert(policy.state==before);
 fail_task=true;clock_ms=next_ms;tracker_internet_poll(true,positions_enabled);assert(!busy);fail_task=false;
 assert(allocated==cleaned&&workers==deleted);
 for(unsigned format=3;format<=5;format++){uint8_t b[38]={0};b[0]=format;b[1]=38;for(unsigned state=0;state<3;state++){internet_position(b,format==4?23:format==5?38:24,state);assert((b[1]&63)==38&&(b[1]>>6)==state+1);}}
 uint8_t small[11]={0x62};internet_position(small,11,2);assert(small[1]==0);
 assert(request("{\"automatic\":false,\"position\":false,\"events\":false}")==200);assert(tracker_internet_position_state()==255);
 puts("Internet: asynchronous worker, bounded HTTP, fallback/captive redirect rejection, debounce, coalescing, stale link results, cooldown, task allocation failure, credentials-free backup config and compact wire flags PASS");
}
