#include "tracker_display.h"
#include "st7735.h"
#include "display_policy.h"
#include "display_language.h"
#define MSG(c,key) display_text((c).language,DISPLAY_TEXT_##key)
#include "tracker_ota.h"
#include "tracker_credentials.h"
#include "tracker_backup.h"
#include "lorawan_port.h"
#include "sx1262.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>

#define WIDTH 160
#define HEIGHT 80
#define BLACK 0x0000
#define WHITE 0xFFFF
#define GREY 0xAD55
#define GREEN 0x5FEA
#define YELLOW 0xFFE0
#define CYAN 0x07FF
#define NAVY 0x10AC
static const uint16_t font[] = {
#include "font7x10.inc"
};
/* One whole frame per refresh avoids per-pixel SPI transactions/flicker. */
static uint8_t framebuffer[WIDTH*HEIGHT*2];
static tracker_sat_snapshot_t satellites;

typedef struct {char ssid[33],ip[16],hotspot_ssid[33];uint8_t wifi_state,channel,auth;int16_t rssi;bool signal_known,hotspot_on,hotspot_open,hotspot_manual,track_on,track_ready,stats_on,stats_ready,track_known,stats_known;uint32_t connected_s,track_interval,track_count,track_capacity,stats_interval,stats_count,stats_capacity;} display_services_t;
static display_services_t services;
static const cJSON *item(const cJSON *j,const char *key){return cJSON_GetObjectItemCaseSensitive(j,key);}
static uint32_t number(const cJSON *j,const char *key){const cJSON *v=item(j,key);return cJSON_IsNumber(v)&&v->valuedouble>=0?(uint32_t)v->valuedouble:0;}
static void ascii_copy(char *out,size_t capacity,const cJSON *v){const unsigned char *p=(const unsigned char *)(cJSON_IsString(v)?v->valuestring:"");size_t n=0;while(*p&&n+1<capacity){unsigned char c=*p++;if(c>=128){while((*p&0xc0)==0x80)p++;c='?';}out[n++]=c>=32&&c<127?c:'?';}out[n]=0;}
void tracker_display_services(const cJSON *root){
 const cJSON *wifi=item(root,"wifi"),*track=item(root,"track"),*stats=item(root,"sat_history"),*state=item(wifi,"state"),*rssi=item(wifi,"rssi_dbm");
 memset(&services,0,sizeof(services));ascii_copy(services.ssid,sizeof(services.ssid),item(wifi,"ssid"));ascii_copy(services.ip,sizeof(services.ip),item(wifi,"ip"));ascii_copy(services.hotspot_ssid,sizeof(services.hotspot_ssid),item(wifi,"hotspot_ssid"));
 services.wifi_state=cJSON_IsString(state)?(!strcmp(state->valuestring,"connected")?2:!strcmp(state->valuestring,"connecting")?1:0):0;services.signal_known=cJSON_IsNumber(rssi);if(services.signal_known)services.rssi=rssi->valueint;services.channel=number(wifi,"channel");services.auth=number(wifi,"auth_mode");services.connected_s=number(wifi,"connected_seconds");services.hotspot_on=cJSON_IsTrue(item(wifi,"setup_active"));services.hotspot_open=cJSON_IsTrue(item(wifi,"hotspot_open"));services.hotspot_manual=cJSON_IsTrue(item(wifi,"hotspot_manual"));
 services.track_known=cJSON_IsBool(item(track,"ready"));services.stats_known=cJSON_IsBool(item(stats,"ready"));services.track_on=cJSON_IsTrue(item(track,"enabled"));services.track_ready=cJSON_IsTrue(item(track,"ready"));services.track_interval=number(track,"interval_s");services.track_count=number(track,"count");services.track_capacity=number(track,"capacity");services.stats_on=cJSON_IsTrue(item(stats,"enabled"));services.stats_ready=cJSON_IsTrue(item(stats,"ready"));services.stats_interval=number(stats,"interval_s");services.stats_count=number(stats,"count");services.stats_capacity=number(stats,"capacity");
}

static display_config_t settings={.version=5,.language=1,.mode=DISPLAY_ON,.brightness=100,.boot_on=1,.timeout_s=60,.highlight=3,.start_page=0,.order={0,1,2,3,4,5,6,7,8},.saver_brightness=15,.clock_zone=1,.clock_seconds=1,.enabled_mask=255,.timezone="Europe/Berlin"};
static portMUX_TYPE display_mux=portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t frame_mutex;
static unsigned revision=1,applied;
static bool wake_requested,display_on=true,pwm_supported,frame_ready,initialized,refresh_requested;
static display_runtime_t runtime;
static unsigned button_action,last_manual_page;static int64_t manual_dirty_since;static bool manual_dirty;
static int64_t lora_last_ms,lora_next_ms;static uint8_t lora_next_port;

static unsigned last_brightness=101;
static const char *mode_name(unsigned mode){return mode==2?"clock":mode==DISPLAY_AUTO?"auto":"on";}
bool tracker_display_is_on(void){portENTER_CRITICAL(&display_mux);bool on=display_on&&!runtime.saver;portEXIT_CRITICAL(&display_mux);return on;}
void tracker_display_button(unsigned event){portENTER_CRITICAL(&display_mux);button_action=event;portEXIT_CRITICAL(&display_mux);}
bool tracker_display_refresh_requested(void){portENTER_CRITICAL(&display_mux);bool refresh=refresh_requested;refresh_requested=false;portEXIT_CRITICAL(&display_mux);return refresh;}
void tracker_display_lora_timing(int64_t last,int64_t next,uint8_t port){lora_last_ms=last;lora_next_ms=next;lora_next_port=port;}
void tracker_display_wake(void){portENTER_CRITICAL(&display_mux);wake_requested=true;portEXIT_CRITICAL(&display_mux);}
void tracker_display_poll(int64_t now){
 portENTER_CRITICAL(&display_mux);
 display_config_t c=settings;
 if(revision!=applied){if(!initialized)display_runtime_start(&runtime,c,last_manual_page,now);else display_runtime_interact(&runtime,c,now);applied=revision;initialized=true;refresh_requested=true;}
 if(button_action){bool was_saver=runtime.saver;display_runtime_interact(&runtime,c,now);if(button_action==DISPLAY_BUTTON_HOME)runtime.page=0;else if(button_action==DISPLAY_BUTTON_NEXT&&display_on&&!was_saver)runtime.page=display_next_page(c,runtime.page);if(button_action==DISPLAY_BUTTON_HOME||button_action==DISPLAY_BUTTON_NEXT){if(last_manual_page!=runtime.page){last_manual_page=runtime.page;manual_dirty=true;manual_dirty_since=now;}}button_action=0;refresh_requested=true;}
 if(wake_requested){display_runtime_interact(&runtime,c,now);wake_requested=false;refresh_requested=true;}
 if(display_runtime_tick(&runtime,c,now))refresh_requested=true;
 display_on=c.mode==2||display_visible(c.mode,now,runtime.deadline);unsigned level=display_on?(runtime.saver?c.saver_brightness:c.brightness):0;
 bool save=manual_dirty&&now-manual_dirty_since>=5000;unsigned remembered=last_manual_page;
 portEXIT_CRITICAL(&display_mux);
 if(save&&!tracker_backup_locked()){nvs_handle_t h;if(nvs_open("tracker_display",NVS_READWRITE,&h)==ESP_OK){esp_err_t err=nvs_set_u8(h,"last_page",remembered);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);if(err==ESP_OK){portENTER_CRITICAL(&display_mux);if(last_manual_page==remembered)manual_dirty=false;portEXIT_CRITICAL(&display_mux);}}}
 if(level!=last_brightness){bool ok=tft_set_brightness(level);if(!ok)tft_backlight(level>0);portENTER_CRITICAL(&display_mux);pwm_supported=ok;portEXIT_CRITICAL(&display_mux);last_brightness=level;}
}
void tracker_display_json(cJSON *root){
 portENTER_CRITICAL(&display_mux);display_config_t c=settings;bool on=display_on,pwm=pwm_supported;unsigned page=runtime.page,last=last_manual_page;bool saver=runtime.saver;int64_t deadline=runtime.deadline,next=runtime.next_rotation;portEXIT_CRITICAL(&display_mux);
 int64_t now=esp_timer_get_time()/1000;cJSON *j=cJSON_AddObjectToObject(root,"display");if(!j)return;
 cJSON *order=cJSON_AddArrayToObject(j,"page_order"),*enabled=cJSON_AddArrayToObject(j,"enabled_pages");for(unsigned i=0;i<DISPLAY_PAGE_COUNT;i++){if(order)cJSON_AddItemToArray(order,cJSON_CreateNumber(c.order[i]));if(enabled&&(c.enabled_mask&(1u<<c.order[i])))cJSON_AddItemToArray(enabled,cJSON_CreateNumber(c.order[i]));}
 cJSON_AddNumberToObject(j,"language",c.language);cJSON_AddNumberToObject(j,"rotate_s",c.rotate_s);cJSON_AddNumberToObject(j,"start_page",c.start_page);cJSON_AddNumberToObject(j,"last_manual_page",last);cJSON_AddNumberToObject(j,"saver_brightness",c.saver_brightness);cJSON_AddStringToObject(j,"clock_zone",c.timezone);cJSON_AddBoolToObject(j,"clock_seconds",c.clock_seconds);cJSON_AddBoolToObject(j,"screensaver",saver);cJSON_AddNumberToObject(j,"active_brightness",on?(saver?c.saver_brightness:c.brightness):0);cJSON_AddNumberToObject(j,"next_rotation_s",c.rotate_s&&!saver&&on&&next>now?(next-now+999)/1000:0);
 cJSON_AddStringToObject(j,"mode",mode_name(c.mode));cJSON_AddBoolToObject(j,"boot_on",c.boot_on);cJSON_AddNumberToObject(j,"highlight",c.highlight);cJSON_AddNumberToObject(j,"page",page);cJSON_AddNumberToObject(j,"timeout_s",c.timeout_s);cJSON_AddNumberToObject(j,"brightness",c.brightness);cJSON_AddBoolToObject(j,"brightness_supported",pwm);cJSON_AddBoolToObject(j,"on",on);cJSON_AddNumberToObject(j,"remaining_s",c.mode!=DISPLAY_ON&&!saver&&on&&deadline>now?(deadline-now+999)/1000:0);cJSON_AddBoolToObject(j,"controller_sleep",false);
}
static esp_err_t display_post(httpd_req_t *req){
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Request token required");
 if(req->content_len<2||req->content_len>512)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid size");
 char body[513];int n=0;while(n<req->content_len){int got=httpd_req_recv(req,body+n,req->content_len-n);if(got<=0)return ESP_FAIL;n+=got;}body[n]=0;
 cJSON *j=cJSON_Parse(body),*action=cJSON_GetObjectItemCaseSensitive(j,"action");
 if(cJSON_IsString(action)&&!strcmp(action->valuestring,"wake")){cJSON_Delete(j);tracker_display_wake();httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");}
 if(cJSON_IsString(action)&&!strcmp(action->valuestring,"page")){
  cJSON *page=cJSON_GetObjectItemCaseSensitive(j,"page");bool valid=cJSON_IsNumber(page)&&page->valuedouble==page->valueint&&page->valueint>=0&&page->valueint<DISPLAY_PAGE_COUNT;
  if(valid){portENTER_CRITICAL(&display_mux);if(runtime.saver){runtime.saver=false;}runtime.page=page->valueint;if(last_manual_page!=runtime.page){last_manual_page=runtime.page;manual_dirty=true;manual_dirty_since=esp_timer_get_time()/1000;}runtime.next_rotation=esp_timer_get_time()/1000+(int64_t)(settings.rotate_s>30?settings.rotate_s:30)*1000;refresh_requested=true;if(display_on)wake_requested=true;portEXIT_CRITICAL(&display_mux);}cJSON_Delete(j);
  if(!valid){return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid display page");}
  httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
 }
 cJSON *mode=cJSON_GetObjectItemCaseSensitive(j,"mode"),*timeout=cJSON_GetObjectItemCaseSensitive(j,"timeout_s"),*brightness=cJSON_GetObjectItemCaseSensitive(j,"brightness"),*boot=cJSON_GetObjectItemCaseSensitive(j,"boot_on"),*highlight=cJSON_GetObjectItemCaseSensitive(j,"highlight");
 int m=cJSON_IsString(mode)?(!strcmp(mode->valuestring,"on")?DISPLAY_ON:!strcmp(mode->valuestring,"auto")?DISPLAY_AUTO:!strcmp(mode->valuestring,"clock")?2:!strcmp(mode->valuestring,"off")?DISPLAY_OFF:-1):-1;
 portENTER_CRITICAL(&display_mux);display_config_t c=settings;portEXIT_CRITICAL(&display_mux);
 bool valid=m>=0&&cJSON_IsNumber(timeout)&&timeout->valuedouble==timeout->valueint&&display_timeout_valid(timeout->valueint)&&cJSON_IsNumber(brightness)&&brightness->valuedouble==brightness->valueint&&brightness->valueint>=1&&brightness->valueint<=100&&(!boot||cJSON_IsBool(boot))&&(!highlight||(cJSON_IsNumber(highlight)&&highlight->valuedouble==highlight->valueint&&highlight->valueint>=0&&highlight->valueint<=7));
 if(valid){c.version=5;c.mode=m==2&&strcmp(mode->valuestring,"off")?2:m==DISPLAY_ON?DISPLAY_ON:DISPLAY_AUTO;c.brightness=brightness->valueint;c.timeout_s=timeout->valueint;if(boot)c.boot_on=cJSON_IsTrue(boot);if(!strcmp(mode->valuestring,"off"))c.boot_on=false;if(m==DISPLAY_ON)c.boot_on=true;if(highlight)c.highlight=highlight->valueint;}
 cJSON *order=cJSON_GetObjectItemCaseSensitive(j,"page_order"),*enabled=cJSON_GetObjectItemCaseSensitive(j,"enabled_pages");
 if(order){if(!cJSON_IsArray(order)||cJSON_GetArraySize(order)!=DISPLAY_PAGE_COUNT)valid=false;else for(unsigned i=0;i<DISPLAY_PAGE_COUNT;i++){cJSON *v=cJSON_GetArrayItem(order,i);if(!cJSON_IsNumber(v)||v->valuedouble!=v->valueint||v->valueint<0||v->valueint>=DISPLAY_PAGE_COUNT){valid=false;break;}c.order[i]=v->valueint;}}
 if(enabled){if(!cJSON_IsArray(enabled)||cJSON_GetArraySize(enabled)<1||cJSON_GetArraySize(enabled)>DISPLAY_PAGE_COUNT)valid=false;else{unsigned mask=0;for(int i=0;i<cJSON_GetArraySize(enabled);i++){cJSON *v=cJSON_GetArrayItem(enabled,i);if(!cJSON_IsNumber(v)||v->valuedouble!=v->valueint||v->valueint<0||v->valueint>=DISPLAY_PAGE_COUNT||(mask&(1u<<v->valueint))){valid=false;break;}mask|=1u<<v->valueint;}c.enabled_mask=mask;}}
 const char *keys[]={"rotate_s","start_page","saver_brightness"};uint8_t *fields[]={&c.rotate_s,&c.start_page,&c.saver_brightness};for(unsigned i=0;i<3;i++){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,keys[i]);if(v){if(!cJSON_IsNumber(v)||v->valuedouble!=v->valueint||v->valueint<0||v->valueint>255)valid=false;else *fields[i]=v->valueint;}}
 cJSON *zone=cJSON_GetObjectItemCaseSensitive(j,"clock_zone");if(zone){if(!cJSON_IsString(zone)||strlen(zone->valuestring)>=sizeof(c.timezone)||tracker_timezone_find(zone->valuestring)<0)valid=false;else strcpy(c.timezone,zone->valuestring);}
 cJSON *seconds=cJSON_GetObjectItemCaseSensitive(j,"clock_seconds");if(seconds){if(!cJSON_IsBool(seconds))valid=false;else c.clock_seconds=cJSON_IsTrue(seconds);}
 cJSON *language=cJSON_GetObjectItemCaseSensitive(j,"language");if(language){if(!cJSON_IsNumber(language)||language->valuedouble!=language->valueint||language->valueint<0||language->valueint>1)valid=false;else c.language=language->valueint;}
 valid=valid&&display_config_valid(c);
 cJSON_Delete(j);
 if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid display settings");
 nvs_handle_t h;esp_err_t err=nvs_open("tracker_display",NVS_READWRITE,&h);if(err==ESP_OK){err=nvs_set_blob(h,"config_v5",&c,sizeof(c));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save display settings");
 portENTER_CRITICAL(&display_mux);settings=c;revision++;wake_requested=false;portEXIT_CRITICAL(&display_mux);httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
}
static esp_err_t frame_get(httpd_req_t *req){
 if(!frame_mutex||xSemaphoreTake(frame_mutex,pdMS_TO_TICKS(100))!=pdTRUE){httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,"Display preview busy");}
 esp_err_t err;if(frame_ready){httpd_resp_set_type(req,"application/octet-stream");httpd_resp_set_hdr(req,"Cache-Control","no-store");err=httpd_resp_send(req,(const char *)framebuffer,sizeof(framebuffer));}else {httpd_resp_set_status(req,"503 Service Unavailable");err=httpd_resp_sendstr(req,"Display preview starting");}xSemaphoreGive(frame_mutex);return err;
}
void tracker_display_register(httpd_handle_t server){const httpd_uri_t routes[]={{.uri="/api/display",.method=HTTP_POST,.handler=display_post},{.uri="/api/display/frame",.method=HTTP_GET,.handler=frame_get}};for(unsigned i=0;i<2;i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));}

static void pixel(unsigned x,unsigned y,uint16_t color) {
    if(x>=WIDTH||y>=HEIGHT)return;
    size_t i=2*(y*WIDTH+x);framebuffer[i]=(uint8_t)(color>>8);framebuffer[i+1]=(uint8_t)color;
}
static void rectangle(unsigned x,unsigned y,unsigned w,unsigned h,uint16_t color) {
    for(unsigned row=y;row<y+h&&row<HEIGHT;row++)
        for(unsigned col=x;col<x+w&&col<WIDTH;col++)pixel(col,row,color);
}
static void text(unsigned x,unsigned y,const char *s,uint16_t color,unsigned scale) {
    for(;*s;s++,x+=7*scale) {
        unsigned c=(unsigned char)*s;if(c<32||c>127)c='?';
        for(unsigned row=0;row<10;row++)for(unsigned col=0;col<7;col++)
            if((font[(c-32)*10+row]<<col)&0x8000u)
                rectangle(x+col*scale,y+row*scale,scale,scale,color);
    }
}
static void count_pair(char out[8],unsigned used,unsigned visible,bool used_known,bool visible_known) {
    char a[4],b[4];
    if(used_known)snprintf(a,sizeof(a),"%u",used);else strcpy(a,"--");
    if(visible_known)snprintf(b,sizeof(b),"%u",visible);else strcpy(b,"--");
    snprintf(out,8,"%s/%s",a,b);
}
static void page_count(display_config_t c,unsigned page){char label[8];unsigned count=0,position=0;for(unsigned i=0;i<DISPLAY_PAGE_COUNT;i++)if(c.enabled_mask&(1u<<c.order[i])){count++;if(c.order[i]==page)position=count;}if(position){label[0]=(char)('0'+position);label[1]='/';label[2]=(char)('0'+count);label[3]=0;}else snprintf(label,sizeof(label),page==0?"HOME":"--");text(WIDTH-2-strlen(label)*7,0,label,GREY,1);}
static void ssid_rows(const char *ssid,unsigned y){char line[24];snprintf(line,sizeof(line),"SSID %.17s",ssid[0]?ssid:"--");text(2,y,line,WHITE,1);if(strlen(ssid)>17)text(2,y+10,ssid+17,WHITE,1);}
static const char *reset_name(display_config_t config){switch(esp_reset_reason()){case ESP_RST_POWERON:return MSG(config,RESET_POWER);case ESP_RST_SW:return MSG(config,RESET_SOFTWARE);case ESP_RST_PANIC:return MSG(config,RESET_PANIC);case ESP_RST_INT_WDT:case ESP_RST_TASK_WDT:case ESP_RST_WDT:return MSG(config,RESET_WATCHDOG);case ESP_RST_BROWNOUT:return MSG(config,RESET_BROWNOUT);default:return MSG(config,RESET_OTHER);}}
static void draw_clock(const tracker_gnss_t *g,int64_t now,display_config_t c){
 text(2,0,MSG(c,CLOCK),CYAN,1);page_count(c,8);
 bool known=g->utc_confirmed&&g->utc_received_ms>=0&&now>=g->utc_received_ms;
 if(!known){text(10,24,"--:--",WHITE,3);text(2,60,MSG(c,WAIT_GNSS_TIME),YELLOW,1);return;}
 time_t epoch=(g->utc_epoch_ms+now-g->utc_received_ms)/1000;int offset=0;bool zone_valid=tracker_timezone_offset(c.timezone,epoch,&offset);epoch+=offset*60;struct tm local;gmtime_r(&epoch,&local);char line[32];
 strftime(line,sizeof(line),c.clock_seconds?"%H:%M:%S":"%H:%M",&local);unsigned scale=c.clock_seconds?2:3;text((WIDTH-strlen(line)*7*scale)/2,22,line,WHITE,scale);
 if(c.language==1)snprintf(line,sizeof(line),"%s %02d %s %04d",display_weekday(c.language,local.tm_wday),local.tm_mday,display_month(local.tm_mon),local.tm_year+1900);else snprintf(line,sizeof(line),"%s %02d.%02d.%04d",display_weekday(c.language,local.tm_wday),local.tm_mday,local.tm_mon+1,local.tm_year+1900);text((WIDTH-strlen(line)*7)/2,49,line,CYAN,1);
 const char *city=strrchr(c.timezone,'/');city=city?city+1:c.timezone;char name[10];snprintf(name,sizeof(name),"%.9s",city);for(char *p=name;*p;p++)if(*p=='_')*p=' ';snprintf(line,sizeof(line),"%s UTC%c%02d:%02d",name,offset<0?'-':'+',abs(offset)/60,abs(offset)%60);text(2,60,line,GREY,1);text(2,70,!zone_valid?MSG(c,ZONE_FALLBACK):now-g->utc_received_ms<10000?MSG(c,GNSS_TIME):MSG(c,GNSS_EXTRAPOLATED),!zone_valid||now-g->utc_received_ms>=10000?YELLOW:GREEN,1);
}
void tracker_display_init(void) {
    frame_mutex=xSemaphoreCreateMutex();
    nvs_handle_t h;if(nvs_open("tracker_display",NVS_READONLY,&h)==ESP_OK){display_config_t c;size_t n=sizeof(c);if(nvs_get_blob(h,"config_v5",&c,&n)==ESP_OK&&n==sizeof(c)&&display_config_valid(c))settings=c;else{display_config_v4_t old4;n=sizeof(old4);if(nvs_get_blob(h,"config_v4",&old4,&n)==ESP_OK&&n==sizeof(old4)&&display_config_migrate_v4(old4,&settings)){}else{display_config_v3_t old3;n=sizeof(old3);if(nvs_get_blob(h,"config_v3",&old3,&n)==ESP_OK&&n==sizeof(old3)&&display_config_migrate_v3(old3,&settings)){}else{display_config_v2_t old2;n=sizeof(old2);if(nvs_get_blob(h,"config_v2",&old2,&n)==ESP_OK&&n==sizeof(old2)&&display_config_migrate_v2(old2,&settings)){}else{display_config_v1_t old;n=sizeof(old);if(nvs_get_blob(h,"config_v1",&old,&n)==ESP_OK&&n==sizeof(old))display_config_migrate(old,&settings);}}}}uint8_t last=0;if(nvs_get_u8(h,"last_page",&last)==ESP_OK&&last<9)last_manual_page=last;nvs_close(h);}
    tft_init();
    /* Official Wireless Tracker V2 factory driver disables color inversion. */
    tft_invert_colors(false);
    tracker_display_poll(esp_timer_get_time()/1000);
}
void tracker_display_update(const tracker_gnss_t *g,int64_t now,bool joined) {
    if(!frame_mutex||xSemaphoreTake(frame_mutex,0)!=pdTRUE)return;
    uint8_t payload[POSITION_BYTES];
    tracker_position_payload(g,now,0,payload);
    tracker_sat_snapshot(g,now,0,&satellites);
    bool fix=tracker_gnss_has_fix(g,now);
    unsigned used[8]={0},visible[8]={0};
    for(unsigned i=0;i<satellites.count;i++) {
        const tracker_sat_view_t *s=&satellites.satellites[i];
        if(s->system>7||s->system==0)continue;
        if(s->flags&SAT_USED)used[s->system]++;
        if(s->flags&SAT_VISIBLE)visible[s->system]++;
    }
    char pairs[8][8];
    for(unsigned i=1;i<=7;i++)count_pair(pairs[i],used[i],visible[i],
        (payload[37]&(1<<(i-1)))!=0,(payload[38]&(1<<(i-1)))!=0);
    portENTER_CRITICAL(&display_mux);display_config_t config=settings;unsigned page=runtime.page,highlight=config.highlight;portEXIT_CRITICAL(&display_mux);
    memset(framebuffer,0,sizeof(framebuffer));char line[40],total[8];count_pair(total,payload[14],payload[15],payload[14]!=255,payload[15]!=255);
    static const char *const names[]={"TOTAL","GPS","GLONASS","GALILEO","BEIDOU","QZSS","SBAS","NAVIC"};
    static const char *const abbr[]={"ALL","GPS","GLO","GAL","BDS","QZS","SBA","NAV"};
    if(page==0){
        const char *status=fix?"FIX OK":(payload[1]&4)?MSG(config,NO_FIX):"GNSS --";text(2,0,status,fix?GREEN:YELLOW,1);text(78,0,joined?"LoRa OK":tracker_credentials_ready()?"JOIN...":"LoRa --",joined?CYAN:YELLOW,1);page_count(config,page);
        if(fix)snprintf(line,sizeof(line),"%.6f %.6f",g->latitude,g->longitude);else snprintf(line,sizeof(line),"Position: --");text(2,10,line,WHITE,1);
        unsigned y=highlight?30:20;if(highlight){snprintf(line,sizeof(line),"Fix/Total %s",total);text(2,20,line,GREY,1);}
        bool used_now=highlight?fix&&used[highlight]>0:fix,reported=highlight?visible[highlight]>0:payload[15]!=255&&payload[15]>0;
        const char *pair=highlight?pairs[highlight]:total;uint16_t bg=used_now?GREEN:reported?YELLOW:NAVY,fg=used_now||reported?BLACK:CYAN;
        rectangle(0,y,WIDTH,20,bg);text(2,y,names[highlight],fg,1);text(2,y+10,used_now?MSG(config,IN_FIX):reported?MSG(config,REPORTED):MSG(config,NO_DATA),fg,1);text(WIDTH-2-strlen(pair)*14,y,pair,fg,2);
        unsigned row=y+20,column=0;for(unsigned i=1;i<=7;i++){if(i==highlight)continue;snprintf(line,sizeof(line),"%s %s",abbr[i],pairs[i]);text(column?82:2,row,line,WHITE,1);column++;if(column==2){column=0;row+=10;}}
        if(!highlight)text(82,70,"Fix/Total",GREY,1);
    }else if(page==1){
        text(2,0,"SAT Fix/Total",CYAN,1);page_count(config,page);
        for(unsigned i=1;i<=7;i++){uint16_t color=fix&&used[i]?GREEN:visible[i]?WHITE:GREY;text(2,i*10,names[i],color,1);text(110,i*10,pairs[i],color,1);}
    }else if(page==2){
        text(2,0,"POSITION",CYAN,1);page_count(config,page);
        if(fix)snprintf(line,sizeof(line),"Lat %.6f",g->latitude);else snprintf(line,sizeof(line),"Lat --");text(2,10,line,WHITE,1);
        if(fix)snprintf(line,sizeof(line),"Lon %.6f",g->longitude);else snprintf(line,sizeof(line),"Lon --");text(2,20,line,WHITE,1);
        int16_t altitude=(int16_t)(((uint16_t)payload[10]<<8)|payload[11]);if(altitude!=INT16_MIN)snprintf(line,sizeof(line),MSG(config,ALTITUDE),altitude);else snprintf(line,sizeof(line),"%s",MSG(config,NO_ALTITUDE));text(2,30,line,GREY,1);
        unsigned speed=((unsigned)payload[20]<<8)|payload[21];if(speed!=65535)snprintf(line,sizeof(line),MSG(config,SPEED),speed*.036);else snprintf(line,sizeof(line),"%s",MSG(config,NO_SPEED));text(2,40,line,GREY,1);text(2,50,fix?"FIX OK":MSG(config,NO_FIX),fix?GREEN:YELLOW,1);
        if(g->utc_confirmed&&g->utc_received_ms>=0&&now>=g->utc_received_ms&&now-g->utc_received_ms<10000){time_t epoch=(g->utc_epoch_ms+now-g->utc_received_ms)/1000;struct tm utc;gmtime_r(&epoch,&utc);strftime(line,sizeof(line),"UTC %Y-%m-%d",&utc);text(2,60,line,CYAN,1);strftime(line,sizeof(line),"%H:%M:%S UTC",&utc);text(2,70,line,CYAN,1);}else text(2,60,"UTC --",GREY,1);
    }else if(page==3){
        text(2,0,"LoRaWAN",CYAN,1);page_count(config,page);text(2,10,joined?MSG(config,SESSION_ACTIVE):tracker_credentials_ready()?MSG(config,JOIN_PENDING):MSG(config,CREDENTIALS_MISSING),joined?GREEN:YELLOW,1);
        MibRequestConfirm_t mib={.Type=MIB_CHANNELS_DATARATE};uint8_t sf=0;uint16_t bw=0;bool fsk=false;if(lorawan_port_is_inited()&&LoRaMacMibGetRequestConfirm(&mib)==LORAMAC_STATUS_OK&&lorawan_port_get_dr_params(mib.Param.ChannelsDatarate,&sf,&bw,&fsk)&&!fsk)snprintf(line,sizeof(line),"SF%u / %u kHz",sf,bw);else snprintf(line,sizeof(line),"%s",MSG(config,NO_DATARATE));text(2,20,line,WHITE,1);
        sx1262_tx_power_t power;if(sx1262_get_last_tx_power(&power))snprintf(line,sizeof(line),MSG(config,TX_TARGET),power.target_dbm);else snprintf(line,sizeof(line),"%s",MSG(config,NO_TX_TARGET));text(2,30,line,GREY,1);
        lorawan_uplink_info_t up;if(lorawan_port_get_last_uplink(&up))snprintf(line,sizeof(line),MSG(config,COUNTER),(unsigned long)up.counter);else snprintf(line,sizeof(line),"%s",MSG(config,NO_COUNTER));text(2,40,line,GREY,1);
        if(lora_last_ms>0)snprintf(line,sizeof(line),MSG(config,LAST_TX),(unsigned long)((now-lora_last_ms)/1000));else snprintf(line,sizeof(line),"%s",MSG(config,NO_LAST_TX));text(2,50,line,GREY,1);
        if(lora_next_ms>now)snprintf(line,sizeof(line),MSG(config,NEXT_TX),(unsigned long)((lora_next_ms-now+999)/1000));else snprintf(line,sizeof(line),"%s",MSG(config,TX_PENDING));text(2,60,line,CYAN,1);text(2,70,lora_next_port==11?MSG(config,SATELLITE_LIST):MSG(config,POSITION_STATUS),GREY,1);
    }else if(page==4){
        text(2,0,MSG(config,NETWORK),CYAN,1);page_count(config,page);text(2,10,services.wifi_state==2?MSG(config,CONNECTED):services.wifi_state==1?MSG(config,CONNECTING):MSG(config,NO_WIFI_IP),services.wifi_state==2?GREEN:YELLOW,1);ssid_rows(services.ssid,20);
        snprintf(line,sizeof(line),"IP %s",services.wifi_state==2&&services.ip[0]?services.ip:"--");text(2,40,line,WHITE,1);
        if(services.signal_known)snprintf(line,sizeof(line),MSG(config,RSSI_CHANNEL),services.rssi,services.channel);else snprintf(line,sizeof(line),"Signal --");text(2,50,line,GREY,1);
        static const char *const auth[]={"Offen","WEP","WPA","WPA2","WPA/WPA2","Enterprise","WPA3","WPA2/WPA3"};snprintf(line,sizeof(line),MSG(config,SECURITY),services.wifi_state==2&&services.auth<8?(services.auth==0?MSG(config,OPEN):auth[services.auth]):"--");text(2,60,line,GREY,1);
        if(services.wifi_state==2)snprintf(line,sizeof(line),MSG(config,CONNECTED_MINUTES),(unsigned long)(services.connected_s/60));else snprintf(line,sizeof(line),"%s",services.wifi_state==1?MSG(config,CONNECTION_ATTEMPT):MSG(config,NO_PROFILE));text(2,70,line,GREY,1);
    }else if(page==5){
        text(2,0,"HOTSPOT",CYAN,1);page_count(config,page);rectangle(0,10,WIDTH,20,services.hotspot_on?GREEN:NAVY);text(2,10,services.hotspot_on?MSG(config,ON):MSG(config,OFF),services.hotspot_on?BLACK:WHITE,2);text(80,15,services.hotspot_manual?MSG(config,MANUAL):"AUTO",services.hotspot_on?BLACK:GREY,1);ssid_rows(services.hotspot_ssid,30);text(2,50,"IP 192.168.4.1",WHITE,1);text(2,60,services.hotspot_open?MSG(config,OPEN):MSG(config,PASSWORD),services.hotspot_open?YELLOW:GREY,1);text(2,70,services.hotspot_on?(services.hotspot_manual?MSG(config,HOTSPOT_CLOSE_WEB):MSG(config,HOTSPOT_AUTOMATIC)):MSG(config,HOTSPOT_USER),GREY,1);
    }else if(page==6){
        text(2,0,MSG(config,DIAGNOSTICS),CYAN,1);page_count(config,page);unsigned long minutes=now/60000;snprintf(line,sizeof(line),MSG(config,UPTIME),minutes/60,minutes%60);text(2,10,line,WHITE,1);snprintf(line,sizeof(line),"Reset %s",reset_name(config));text(2,20,line,GREY,1);
        snprintf(line,sizeof(line),MSG(config,RAM_FREE),(unsigned long)(esp_get_free_heap_size()/1024));text(2,30,line,WHITE,1);snprintf(line,sizeof(line),"Minimum %lu KiB",(unsigned long)(esp_get_minimum_free_heap_size()/1024));text(2,40,line,GREY,1);snprintf(line,sizeof(line),"Block %lu KiB",(unsigned long)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)/1024));text(2,50,line,GREY,1);
        if(g->nmea_ms>=0&&now>=g->nmea_ms)snprintf(line,sizeof(line),MSG(config,GNSS_AGE),(unsigned long)((now-g->nmea_ms)/1000));else snprintf(line,sizeof(line),"GNSS --");text(2,60,line,CYAN,1);snprintf(line,sizeof(line),MSG(config,CRC_ERRORS),(unsigned long)g->checksum_errors);text(2,70,line,GREY,1);
    }else if(page==7){
        text(2,0,MSG(config,RECORDING),CYAN,1);page_count(config,page);
        text(2,10,!services.track_known?"Track --":!services.track_ready?MSG(config,TRACK_STORAGE_ERROR):services.track_on?MSG(config,TRACK_ON):MSG(config,TRACK_OFF),!services.track_known?GREY:services.track_ready?(services.track_on?GREEN:GREY):YELLOW,1);
        snprintf(line,sizeof(line),"%lus %lu/%lu %u%%",(unsigned long)services.track_interval,(unsigned long)services.track_count,(unsigned long)services.track_capacity,services.track_capacity?(unsigned)(100ULL*services.track_count/services.track_capacity):0);text(2,20,services.track_known?line:MSG(config,NO_STORAGE_STATE),WHITE,1);
        text(2,30,!services.stats_known?MSG(config,NO_STATS):!services.stats_ready?MSG(config,STATS_STORAGE_ERROR):services.stats_on?MSG(config,STATS_ON):MSG(config,STATS_OFF),!services.stats_known?GREY:services.stats_ready?(services.stats_on?GREEN:GREY):YELLOW,1);
        snprintf(line,sizeof(line),"%lus %lu/%lu %u%%",(unsigned long)services.stats_interval,(unsigned long)services.stats_count,(unsigned long)services.stats_capacity,services.stats_capacity?(unsigned)(100ULL*services.stats_count/services.stats_capacity):0);text(2,40,services.stats_known?line:MSG(config,NO_STORAGE_STATE),WHITE,1);text(2,50,MSG(config,TRACK_NEEDS_FIX),GREY,1);text(2,60,MSG(config,STATS_WITHOUT_FIX),GREY,1);text(2,70,MSG(config,RING_OLDEST),GREY,1);
    }
    if(page==8)draw_clock(g,now,config);
    frame_ready=true;
    portENTER_CRITICAL(&display_mux);bool on=display_on;portEXIT_CRITICAL(&display_mux);
    if(on)tft_draw_bitmap_rgb565(0,0,WIDTH-1,HEIGHT-1,framebuffer,sizeof(framebuffer));
    xSemaphoreGive(frame_mutex);
}
