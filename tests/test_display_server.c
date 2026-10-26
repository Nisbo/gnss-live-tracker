/* Actual display HTTP policy, persistence and RGB565 renderer with host hardware stubs. */
#include "../main/tracker_display.c"
#include <assert.h>
#include <stddef.h>
static int64_t clock_us=10000000;
static bool authorize=true,have_blob;
static unsigned nvs_writes,frames;
static display_config_t stored;
static uint8_t sent_frame[WIDTH*HEIGHT*2],preview[WIDTH*HEIGHT*2];
static size_t preview_size;
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){assert(!strcmp(name,"tracker_display"));(void)mode;*h=1;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1);}esp_err_t nvs_commit(nvs_handle_t h){assert(h==1);return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n){assert(h==1);if(!have_blob||strcmp(key,"config_v5"))return ESP_ERR_NVS_NOT_FOUND;assert(*n>=sizeof(stored));memcpy(out,&stored,sizeof(stored));*n=sizeof(stored);return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t n){assert(h==1&&!strcmp(key,"config_v5")&&n==76);memcpy(&stored,data,n);have_blob=true;nvs_writes++;return ESP_OK;}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *v){(void)h;(void)key;(void)v;return ESP_ERR_NVS_NOT_FOUND;}esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t v){(void)h;(void)key;(void)v;return ESP_OK;}
bool tracker_request_authorized(httpd_req_t *r){(void)r;return authorize;}bool tracker_backup_locked(void){return false;}bool tracker_credentials_ready(void){return false;}
int64_t esp_timer_get_time(void){return clock_us;}esp_reset_reason_t esp_reset_reason(void){return ESP_RST_POWERON;}uint32_t esp_get_free_heap_size(void){return 130000;}uint32_t esp_get_minimum_free_heap_size(void){return 80000;}size_t heap_caps_get_largest_free_block(unsigned cap){assert(cap==MALLOC_CAP_8BIT);return 48000;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void *)1;}bool xSemaphoreTake(SemaphoreHandle_t mutex,unsigned wait){(void)wait;assert(mutex==(void *)1);return true;}void xSemaphoreGive(SemaphoreHandle_t mutex){assert(mutex==(void *)1);}
void tft_init(void){}void tft_invert_colors(bool invert){assert(!invert);}bool tft_set_brightness(uint8_t brightness){assert(brightness<=100);return true;}void tft_backlight(bool on){(void)on;}
void tft_draw_bitmap_rgb565(uint16_t x0,uint16_t y0,uint16_t x1,uint16_t y1,const void *pixels,size_t n){assert(x0==0&&y0==0&&x1==159&&y1==79&&n==sizeof(sent_frame));memcpy(sent_frame,pixels,n);frames++;}
bool lorawan_port_is_inited(void){return false;}LoRaMacStatus_t LoRaMacMibGetRequestConfirm(MibRequestConfirm_t *m){(void)m;return LORAMAC_STATUS_PARAMETER_INVALID;}bool lorawan_port_get_dr_params(uint8_t dr,uint8_t *sf,uint16_t *bw,bool *fsk){(void)dr;(void)sf;(void)bw;(void)fsk;return false;}bool sx1262_get_last_tx_power(sx1262_tx_power_t *out){(void)out;return false;}bool lorawan_port_get_last_uplink(lorawan_uplink_info_t *out){(void)out;return false;}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *s){r->status=status;snprintf(r->response,sizeof(r->response),"%s",s);return ESP_FAIL;}esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){snprintf(r->response,sizeof(r->response),"%s",s);return ESP_OK;}esp_err_t httpd_resp_send(httpd_req_t *r,const char *data,size_t n){(void)r;assert(n==sizeof(preview));memcpy(preview,data,n);preview_size=n;return ESP_OK;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;(void)k;(void)v;return ESP_OK;}esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){r->status=atoi(s);return ESP_OK;}int httpd_req_recv(httpd_req_t *r,char *out,size_t n){if(n>r->content_len-r->received)n=r->content_len-r->received;memcpy(out,r->body+r->received,n);r->received+=n;return n;}esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *u){(void)h;(void)u;return ESP_OK;}
static int post(const char *body){httpd_req_t r={.body=body,.content_len=strlen(body),.status=200};display_post(&r);return r.status;}
static cJSON *status(void){cJSON *j=cJSON_CreateObject();tracker_display_json(j);return j;}
static void check_preview(void){httpd_req_t request={.status=200};assert(frame_get(&request)==ESP_OK&&preview_size==sizeof(preview));assert(!memcmp(preview,sent_frame,sizeof(preview)));}
/* Compare the actual rendered date row against an independently specified date. */
static void check_date(const char *date){
 memset(framebuffer+49*WIDTH*2,0,10*WIDTH*2);text((WIDTH-strlen(date)*7)/2,49,date,CYAN,1);
 assert(!memcmp(framebuffer+49*WIDTH*2,sent_frame+49*WIDTH*2,10*WIDTH*2));memcpy(framebuffer,sent_frame,sizeof(framebuffer));
}
static void save_frame(unsigned language,unsigned page){char path[128];snprintf(path,sizeof(path),"%s/tracker-display-v290-%s-%u.rgb",getenv("TRACKER_TEST_TMP")?getenv("TRACKER_TEST_TMP"):"/tmp",language?"en":"de",page);FILE *f=fopen(path,"wb");assert(f&&fwrite(sent_frame,1,sizeof(sent_frame),f)==sizeof(sent_frame));assert(!fclose(f));}
int main(void){
 assert(sizeof(display_config_t)==76&&offsetof(display_config_t,language)==23);tracker_display_init();assert(settings.language==1);
 unsigned writes=nvs_writes;authorize=false;assert(post("{\"mode\":\"on\",\"timeout_s\":60,\"brightness\":75,\"language\":1}")==403&&nvs_writes==writes);authorize=true;
 const char *invalid[]={"2","-1","0.5","true","null","\"en\""};for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++){char body[128];snprintf(body,sizeof(body),"{\"mode\":\"on\",\"timeout_s\":60,\"brightness\":75,\"language\":%s}",invalid[i]);assert(post(body)==400&&settings.language==1&&nvs_writes==writes);}
 assert(post("{\"mode\":\"on\",\"timeout_s\":60,\"brightness\":75,\"language\":1}")==200&&settings.language==1&&stored.language==1&&nvs_writes==writes+1);cJSON *j=status();assert(cJSON_GetObjectItem(cJSON_GetObjectItem(j,"display"),"language")->valueint==1);cJSON_Delete(j);
 assert(post("{\"mode\":\"on\",\"timeout_s\":30,\"brightness\":50}")==200&&settings.language==1&&stored.language==1);assert(settings.timeout_s==30&&settings.brightness==50);settings.language=0;tracker_display_init();assert(settings.language==1); /* Persisted reboot restores EN. */
 tracker_gnss_t g;tracker_gnss_init(&g);g.utc_confirmed=true;g.utc_epoch_ms=1792972800000LL;g.utc_received_ms=10000;g.gga_ms=g.nmea_ms=g.last_fix_ms=10000;g.gga_fix=true;g.quality=1;g.latitude=0.25;g.longitude=0.75;g.used=12;
 uint8_t german_clock[sizeof(sent_frame)];
 for(unsigned language=0;language<2;language++){settings.language=language;for(unsigned page=0;page<DISPLAY_PAGE_COUNT;page++){runtime.page=page;tracker_display_update(&g,10000,true);check_preview();save_frame(language,page);if(page==8){check_date(language?"Mon 26 Oct 2026":"Mo 26.10.2026");if(!language)memcpy(german_clock,sent_frame,sizeof(sent_frame));else assert(memcmp(german_clock,sent_frame,sizeof(sent_frame)));}}}
 assert(frames==18); /* Every actual page rendered in both languages; preview is byte-identical. */
 assert(post("{\"mode\":\"on\",\"timeout_s\":60,\"brightness\":100,\"language\":0}")==200&&settings.language==0);cJSON *last=status();assert(cJSON_GetObjectItem(cJSON_GetObjectItem(last,"display"),"language")->valueint==0);cJSON_Delete(last);
 puts("Actual display: optional language HTTP validation, auth/no-write failures, v5 76-byte NVS persistence/reload, all 9 pages DE/EN, differing clock/date glyphs and exact preview framebuffer PASS");
}
