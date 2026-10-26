#include "tracker_web.h"
#include "tracker_internet.h"
#include "lora_messages.h"
#include "radio_settings.h"
#include "tracker_log.h"
#include "sx1262.h"
#include <math.h>
#include "wifi_policy.h"
#include "tracker_ota.h"
#include "tracker_backup.h"
#include "tracker_adr.h"
#include "tracker_credentials.h"
#include "tracker_display.h"
#include "display_policy.h"
#include "lorawan_port.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include "tracker_mqtt.h"
#include "satellite_log.h"
#include "import_log.h"
#include "gnss_diagnostics.h"
#include "status_cache.h"
#include "esp_system.h"

#define PROFILE_COUNT 8
static const char *TAG="tracker-web";
#define TRACKER_SETUP_SSID "Tracker-V2-Setup"
extern const unsigned char page[] asm("_binary_index_html_start");
extern const unsigned char decoder[] asm("_binary_ttn_position_decoder_js_start");
extern const unsigned char catalog[] asm("_binary_satellite_catalog_js_start");
extern const unsigned char dashboard[] asm("_binary_dashboard_js_start");
static status_cache_t status_cache;
static SemaphoreHandle_t status_mutex;
static portMUX_TYPE network_mux=portMUX_INITIALIZER_UNLOCKED;
typedef struct {
    bool configured,connected,setup,apply_pending;
    unsigned failures,disconnects,http_stack_min_free,status_build_failures,status_published_bytes;
    int last_reason;
    int64_t connected_ms,apply_at_ms;
    char ssid[33],ip[16];
    wifi_config_t station;
} network_state_t;
static network_state_t network;
static bool running;
typedef struct { char ssid[33],password[64]; } saved_profile_t;
typedef struct { uint32_t version; saved_profile_t profiles[PROFILE_COUNT]; char ap_ssid[33],ap_password[64]; } saved_wifi_t;
bool tracker_web_backup_valid(const void *data,size_t n){if(n!=sizeof(saved_wifi_t))return false;saved_wifi_t c;memcpy(&c,data,n);bool valid=c.version==1&&memchr(c.ap_ssid,0,33)&&c.ap_ssid[0]&&memchr(c.ap_password,0,64);if(valid&&c.ap_password[0])valid=strlen(c.ap_password)>=8;for(unsigned i=0;i<PROFILE_COUNT&&valid;i++){valid=memchr(c.profiles[i].ssid,0,33)&&memchr(c.profiles[i].password,0,64);if(valid&&c.profiles[i].ssid[0]&&c.profiles[i].password[0])valid=strlen(c.profiles[i].password)>=8;for(unsigned j=0;j<i&&valid;j++)if(c.profiles[i].ssid[0]&&!strcmp(c.profiles[i].ssid,c.profiles[j].ssid))valid=false;}memset(&c,0,sizeof(c));return valid;}
static saved_wifi_t saved;
static int current_profile=-1,locked_profile=-1,pending_profile=-1;
static bool switch_pending,ap_pending,manual_hotspot,hotspot_pending,hotspot_off_pending,hotspot_auto_pending,suppress_hotspot;
static int64_t next_connect,attempt_started,outage_started,next_auto_scan;
static int64_t tried_at[PROFILE_COUNT];
static display_button_t user_button;
static saved_wifi_t saved_copy(void) { saved_wifi_t copy;portENTER_CRITICAL(&network_mux);copy=saved;portEXIT_CRITICAL(&network_mux);return copy; }
static esp_err_t save_wifi(const saved_wifi_t *config) {
    nvs_handle_t handle;esp_err_t err=nvs_open("tracker_web",NVS_READWRITE,&handle);
    if(err==ESP_OK){err=nvs_set_blob(handle,"profiles_v1",config,sizeof(*config));if(err==ESP_OK)err=nvs_commit(handle);nvs_close(handle);}return err;
}
static void load_wifi(void) {
    saved.version=1;strcpy(saved.ap_ssid,TRACKER_SETUP_SSID);
    nvs_handle_t handle;if(nvs_open("tracker_web",NVS_READONLY,&handle)!=ESP_OK)return;
    saved_wifi_t candidate={0};size_t size=sizeof(candidate);
    bool valid=nvs_get_blob(handle,"profiles_v1",&candidate,&size)==ESP_OK&&size==sizeof(candidate)&&candidate.version==1;
    if(valid) {
        valid=memchr(candidate.ap_ssid,0,sizeof(candidate.ap_ssid))&&candidate.ap_ssid[0]&&memchr(candidate.ap_password,0,sizeof(candidate.ap_password));
        for(unsigned i=0;i<PROFILE_COUNT&&valid;i++)valid=memchr(candidate.profiles[i].ssid,0,33)&&memchr(candidate.profiles[i].password,0,64);
    }
    if(valid)saved=candidate;
    else {
        size_t a=33,b=64;
        if(nvs_get_str(handle,"ssid",saved.profiles[0].ssid,&a)!=ESP_OK||nvs_get_str(handle,"password",saved.profiles[0].password,&b)!=ESP_OK)memset(&saved.profiles[0],0,sizeof(saved.profiles[0]));
    }
    nvs_close(handle);memset(&candidate,0,sizeof(candidate));
    /* Migration is additive: the original single-profile keys remain intact. */
    if(!valid)save_wifi(&saved);
}
static bool any_profile(const saved_wifi_t *config) {for(unsigned i=0;i<PROFILE_COUNT;i++)if(config->profiles[i].ssid[0])return true;return false;}

static tracker_sat_snapshot_t web_satellites;
typedef struct {char ssid[33];int rssi;unsigned aps;bool secure;} scan_entry_t;
static scan_entry_t scan_entries[40];
static unsigned scan_count;
static bool scan_busy;
static int scan_error;
static void complete_scan(void) {
    uint16_t count=64;wifi_ap_record_t *records=calloc(count,sizeof(*records));
    if(!records) {portENTER_CRITICAL(&network_mux);scan_busy=false;scan_error=ESP_ERR_NO_MEM;portEXIT_CRITICAL(&network_mux);esp_wifi_clear_ap_list();return;}
    esp_err_t err=esp_wifi_scan_get_ap_records(&count,records);
    scan_entry_t *unique=calloc(40,sizeof(*unique));unsigned n=0;
    if(err==ESP_OK&&unique)for(unsigned i=0;i<count;i++) {
        if(!records[i].ssid[0])continue;
        unsigned j=0;for(;j<n;j++)if(!strcmp(unique[j].ssid,(const char *)records[i].ssid))break;
        if(j==n) {if(n==40)continue;memcpy(unique[n].ssid,records[i].ssid,32);unique[n].ssid[32]=0;unique[n].rssi=records[i].rssi;n++;}
        if(records[i].rssi>unique[j].rssi)unique[j].rssi=records[i].rssi;
        unique[j].aps++;unique[j].secure|=records[i].authmode!=WIFI_AUTH_OPEN;
    }
    portENTER_CRITICAL(&network_mux);
    if(unique)memcpy(scan_entries,unique,n*sizeof(*unique));
    scan_count=n;scan_error=unique?err:ESP_ERR_NO_MEM;scan_busy=false;
    portEXIT_CRITICAL(&network_mux);free(unique);free(records);
}
static network_state_t network_copy(void) {
    network_state_t copy;
    portENTER_CRITICAL(&network_mux);copy=network;portEXIT_CRITICAL(&network_mux);
    return copy;
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
    if(base==WIFI_EVENT&&id==WIFI_EVENT_SCAN_DONE) {
        complete_scan();
    } else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event=data;
        portENTER_CRITICAL(&network_mux);
        network.connected=false;network.ip[0]=0;network.failures++;network.disconnects++;network.last_reason=event->reason;
        if(!outage_started)outage_started=esp_timer_get_time()/1000;
        portEXIT_CRITICAL(&network_mux);
        ESP_LOGI(TAG,"WLAN disconnected, reason=%u",event->reason);
    } else if(base==IP_EVENT&&id==IP_EVENT_STA_LOST_IP) {
        portENTER_CRITICAL(&network_mux);network.connected=false;network.ip[0]=0;if(!outage_started)outage_started=esp_timer_get_time()/1000;portEXIT_CRITICAL(&network_mux);
    } else if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event=data;char ip[16];
        snprintf(ip,sizeof(ip),IPSTR,IP2STR(&event->ip_info.ip));
        portENTER_CRITICAL(&network_mux);
        network.connected=true;network.failures=0;network.last_reason=0;
        network.connected_ms=esp_timer_get_time()/1000;strcpy(network.ip,ip);outage_started=0;
        portEXIT_CRITICAL(&network_mux);
        ESP_LOGI(TAG,"Home WLAN ready: http://%s",ip);
    }
}
static esp_err_t send_text(httpd_req_t *req,const char *type,const char *text) {
    httpd_resp_set_type(req,type);
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    return httpd_resp_send(req,text,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t index_get(httpd_req_t *req) {return send_text(req,"text/html; charset=utf-8",(const char *)page);}
/* Catalogs are compressed at build time and served from flash without a RAM copy. */
extern const unsigned char i18n_bundle_start[] asm("_binary_i18n_bundle_gz_start");
extern const unsigned char i18n_bundle_end[] asm("_binary_i18n_bundle_gz_end");
static esp_err_t i18n_get(httpd_req_t *req){
    httpd_resp_set_type(req,"application/javascript; charset=utf-8");
    httpd_resp_set_hdr(req,"Content-Encoding","gzip");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    return httpd_resp_send(req,(const char *)i18n_bundle_start,i18n_bundle_end-i18n_bundle_start);
}
extern const unsigned char network_page[] asm("_binary_network_js_start");
static esp_err_t network_get(httpd_req_t *req){return send_text(req,"application/javascript",(const char *)network_page);}
static esp_err_t decoder_get(httpd_req_t *req) {return send_text(req,"application/javascript",(const char *)decoder);}
static esp_err_t catalog_get(httpd_req_t *req) {return send_text(req,"application/javascript",(const char *)catalog);}
extern const unsigned char extras[] asm("_binary_extras_js_start");
static esp_err_t extras_get(httpd_req_t *req){return send_text(req,"application/javascript",(const char *)extras);}
extern const uint8_t recording[] asm("_binary_recording_js_start");
extern const unsigned char history_cache_js[] asm("_binary_history_cache_js_start");
static esp_err_t history_cache_get(httpd_req_t *r){return send_text(r,"application/javascript",(const char *)history_cache_js);}
static esp_err_t recording_get(httpd_req_t *req){return send_text(req,"application/javascript",(const char *)recording);}
extern const unsigned char timezones_js[] asm("_binary_timezones_js_start");
static esp_err_t timezones_get(httpd_req_t *r){return send_text(r,"application/javascript",(const char *)timezones_js);}
static esp_err_t dashboard_get(httpd_req_t *req) {return send_text(req,"application/javascript",(const char *)dashboard);}
extern const unsigned char display_js[] asm("_binary_display_js_start");
extern const unsigned char credentials_js[] asm("_binary_credentials_js_start");
extern const unsigned char backup_js[] asm("_binary_backup_js_start");
extern const unsigned char adr_js[] asm("_binary_adr_js_start");
static esp_err_t backup_get(httpd_req_t *r){return send_text(r,"application/javascript",(const char *)backup_js);}
static esp_err_t adr_get(httpd_req_t *r){return send_text(r,"application/javascript",(const char *)adr_js);}
static esp_err_t credentials_get(httpd_req_t *r){return send_text(r,"application/javascript",(const char *)credentials_js);}
static esp_err_t display_get(httpd_req_t *r){return send_text(r,"application/javascript",(const char *)display_js);}
extern const unsigned char firmware_js[] asm("_binary_firmware_js_start");
static esp_err_t firmware_get(httpd_req_t *r){return send_text(r,"application/javascript",(const char *)firmware_js);}
extern const unsigned char orbit_js[] asm("_binary_orbits_js_start");
extern const unsigned char satellite_js[] asm("_binary_satellite_min_js_start");
extern const unsigned char earth_land[] asm("_binary_earth_land_json_start");
extern const unsigned char satellite_license[] asm("_binary_satellite_LICENSE_txt_start");
static esp_err_t orbit_get(httpd_req_t *r){return send_text(r,"application/javascript",(const char *)orbit_js);}
static esp_err_t satellite_js_get(httpd_req_t *r){return send_text(r,"application/javascript",(const char *)satellite_js);}
static esp_err_t earth_land_get(httpd_req_t *r){return send_text(r,"application/json",(const char *)earth_land);}
static esp_err_t satellite_license_get(httpd_req_t *r){return send_text(r,"text/plain; charset=utf-8",(const char *)satellite_license);}
static esp_err_t scan_get(httpd_req_t *req) {
    scan_entry_t *entries=calloc(40,sizeof(*entries));if(!entries)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"No memory");
    portENTER_CRITICAL(&network_mux);unsigned count=scan_count;bool busy=scan_busy;int error=scan_error;memcpy(entries,scan_entries,count*sizeof(*entries));portEXIT_CRITICAL(&network_mux);
    cJSON *root=cJSON_CreateObject();if(!root){free(entries);return ESP_FAIL;}
    cJSON_AddBoolToObject(root,"scanning",busy);cJSON_AddNumberToObject(root,"error",error);
    cJSON *list=cJSON_AddArrayToObject(root,"networks");
    for(unsigned i=0;list&&i<count;i++) {cJSON *item=cJSON_CreateObject();if(!item)continue;cJSON_AddStringToObject(item,"ssid",entries[i].ssid);cJSON_AddNumberToObject(item,"rssi",entries[i].rssi);cJSON_AddNumberToObject(item,"access_points",entries[i].aps);cJSON_AddBoolToObject(item,"secure",entries[i].secure);cJSON_AddItemToArray(list,item);}
    free(entries);char *json=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!json)return ESP_FAIL;
    esp_err_t result=send_text(req,"application/json",json);free(json);return result;
}
static esp_err_t scan_post(httpd_req_t *req) {
    if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Reload this page first");
    portENTER_CRITICAL(&network_mux);bool busy=scan_busy;scan_busy=true;scan_error=0;portEXIT_CRITICAL(&network_mux);
    if(!busy) {wifi_scan_config_t cfg={.show_hidden=false};esp_err_t err=esp_wifi_scan_start(&cfg,false);if(err!=ESP_OK){portENTER_CRITICAL(&network_mux);scan_busy=false;scan_error=err;portEXIT_CRITICAL(&network_mux);}}
    return scan_get(req);
}
static void station_defaults(wifi_config_t *station,bool password_present) {
    station->sta.bssid_set=false;memset(station->sta.bssid,0,sizeof(station->sta.bssid));station->sta.channel=0;
    station->sta.scan_method=WIFI_ALL_CHANNEL_SCAN;station->sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
    station->sta.threshold.authmode=password_present?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    station->sta.pmf_cfg.capable=true;
#if CONFIG_ESP_WIFI_11KV_SUPPORT
    station->sta.rm_enabled=true;station->sta.btm_enabled=true;
#endif
}
static esp_err_t status_get(httpd_req_t *req) {
    unsigned stack_free=uxTaskGetStackHighWaterMark(NULL);
    portENTER_CRITICAL(&network_mux);network.http_stack_min_free=stack_free;portEXIT_CRITICAL(&network_mux);
    if(xSemaphoreTake(status_mutex,pdMS_TO_TICKS(50))!=pdTRUE)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Status busy");
    status_blob_t *blob=status_acquire(&status_cache);xSemaphoreGive(status_mutex);
    if(!blob)return send_text(req,"application/json","{\"starting\":true}");
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    esp_err_t result=httpd_resp_send_chunk(req,blob->data,blob->length-1);
    if(result==ESP_OK)result=httpd_resp_sendstr_chunk(req,",\"sensitive_access_allowed\":true}");
    if(result==ESP_OK)result=httpd_resp_send_chunk(req,NULL,0);
    xSemaphoreTake(status_mutex,portMAX_DELAY);status_release(blob);xSemaphoreGive(status_mutex);return result;
}
static bool valid_ssid(const cJSON *value) {return cJSON_IsString(value)&&strlen(value->valuestring)>0&&strlen(value->valuestring)<=32;}
static bool valid_password(const cJSON *value) {return cJSON_IsString(value)&&(strlen(value->valuestring)==0||(strlen(value->valuestring)>=8&&strlen(value->valuestring)<=63));}
static esp_err_t configure_post(httpd_req_t *req) {
    if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Reload this page first");
    if(req->content_len<2||req->content_len>512)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid request size");
    char body[513];int received=0;
    while(received<req->content_len){int n=httpd_req_recv(req,body+received,req->content_len-received);if(n<=0){memset(body,0,sizeof(body));return ESP_FAIL;}received+=n;}
    body[received]=0;cJSON *json=cJSON_Parse(body);memset(body,0,sizeof(body));
    cJSON *action=cJSON_GetObjectItemCaseSensitive(json,"action"),*ssid=cJSON_GetObjectItemCaseSensitive(json,"ssid"),*password=cJSON_GetObjectItemCaseSensitive(json,"password"),*id=cJSON_GetObjectItemCaseSensitive(json,"id");
    const char *op=cJSON_IsString(action)?action->valuestring:"save";
    saved_wifi_t config=saved_copy();bool valid=true,persist=false,connect=false,deleted_current=false;
    int slot=cJSON_IsNumber(id)?id->valueint:-1;
    if(!strcmp(op,"reveal")||!strcmp(op,"reveal_hotspot")) {
        bool hotspot=!strcmp(op,"reveal_hotspot");
        valid=hotspot||(slot>=0&&slot<PROFILE_COUNT&&config.profiles[slot].ssid[0]);
        cJSON_Delete(json);
        if(!valid){memset(&config,0,sizeof(config));return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid profile");}
        cJSON *reply=cJSON_CreateObject();if(!reply){memset(&config,0,sizeof(config));return ESP_FAIL;}
        cJSON_AddStringToObject(reply,"password",hotspot?config.ap_password:config.profiles[slot].password);char *encoded=cJSON_PrintUnformatted(reply);cJSON_Delete(reply);memset(&config,0,sizeof(config));
        if(!encoded)return ESP_FAIL;
        esp_err_t result=send_text(req,"application/json",encoded);memset(encoded,0,strlen(encoded));free(encoded);return result;
    }
    if(!strcmp(op,"save")) {
        valid=valid_ssid(ssid)&&(!password||valid_password(password));
        if(valid){
            if(slot<0)for(unsigned i=0;i<PROFILE_COUNT;i++)if(!strcmp(config.profiles[i].ssid,ssid->valuestring)){slot=i;break;}
            if(slot<0)for(unsigned i=0;i<PROFILE_COUNT;i++)if(!config.profiles[i].ssid[0]){slot=i;break;}
            valid=slot>=0&&slot<PROFILE_COUNT;
            if(valid){
                for(unsigned i=0;i<PROFILE_COUNT;i++)if((int)i!=slot&&!strcmp(config.profiles[i].ssid,ssid->valuestring))valid=false;
                if(valid){strcpy(config.profiles[slot].ssid,ssid->valuestring);if(password)strcpy(config.profiles[slot].password,password->valuestring);persist=true;connect=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json,"connect"));}
            }
        }
    } else if(!strcmp(op,"delete")) {
        valid=slot>=0&&slot<PROFILE_COUNT&&config.profiles[slot].ssid[0];
        if(valid){memset(&config.profiles[slot],0,sizeof(config.profiles[slot]));persist=true;portENTER_CRITICAL(&network_mux);deleted_current=current_profile==slot;portEXIT_CRITICAL(&network_mux);}
    } else if(!strcmp(op,"connect")) {valid=slot>=0&&slot<PROFILE_COUNT&&config.profiles[slot].ssid[0];connect=valid;}
    else if(!strcmp(op,"lock")) {
        portENTER_CRITICAL(&network_mux);locked_profile=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json,"enabled"))?current_profile:-1;portEXIT_CRITICAL(&network_mux);
    } else if(!strcmp(op,"hotspot")) {
        if(ssid){valid=valid_ssid(ssid)&&(!password||valid_password(password));if(valid){strcpy(config.ap_ssid,ssid->valuestring);if(password)strcpy(config.ap_password,password->valuestring);persist=true;}}
    } else valid=false;
    cJSON *mode=cJSON_GetObjectItemCaseSensitive(json,"mode");
    if(!strcmp(op,"hotspot")&&mode)valid=valid&&cJSON_IsString(mode)&&(!strcmp(mode->valuestring,"auto")||!strcmp(mode->valuestring,"on")||!strcmp(mode->valuestring,"off"));
    esp_err_t err=ESP_OK;if(valid&&persist)err=save_wifi(&config);
    if(valid&&err==ESP_OK){
        portENTER_CRITICAL(&network_mux);
        if(persist){saved=config;network.configured=any_profile(&config);}
        if(deleted_current){switch_pending=true;pending_profile=-1;locked_profile=-1;}
        if(connect){switch_pending=true;pending_profile=slot;locked_profile=-1;}
        if(persist&&!strcmp(op,"hotspot"))ap_pending=true;
        if(!strcmp(op,"hotspot")){
            cJSON *enabled=cJSON_GetObjectItemCaseSensitive(json,"enabled");
            if(mode||cJSON_IsBool(enabled)){hotspot_pending=hotspot_off_pending=hotspot_auto_pending=false;
                if(mode){hotspot_pending=!strcmp(mode->valuestring,"on");hotspot_off_pending=!strcmp(mode->valuestring,"off");hotspot_auto_pending=!strcmp(mode->valuestring,"auto");}
                else if(cJSON_IsTrue(enabled))hotspot_pending=true;else hotspot_off_pending=true;}
        }
        if(switch_pending||ap_pending||hotspot_pending||hotspot_off_pending||hotspot_auto_pending)network.apply_at_ms=esp_timer_get_time()/1000+1500;
        portEXIT_CRITICAL(&network_mux);
    }
    if(password&&cJSON_IsString(password)){memset(password->valuestring,0,strlen(password->valuestring));}
    cJSON_Delete(json);memset(&config,0,sizeof(config));
    if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid profile, duplicate SSID, full profile list or password (empty / 8-63 bytes)");
    if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save WLAN settings");
    return send_text(req,"application/json","{\"ok\":true}");
}
static void apply_hotspot(const saved_wifi_t *config,bool enabled) {
    wifi_config_t ap={0};strcpy((char *)ap.ap.ssid,config->ap_ssid);strcpy((char *)ap.ap.password,config->ap_password);
    ap.ap.channel=1;ap.ap.max_connection=2;ap.ap.authmode=config->ap_password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    esp_err_t err=esp_wifi_set_mode(WIFI_MODE_APSTA);
    if(err==ESP_OK)err=esp_wifi_set_config(WIFI_IF_AP,&ap);
    if(err==ESP_OK&&!enabled)err=esp_wifi_set_mode(WIFI_MODE_STA);
    if(err==ESP_OK){portENTER_CRITICAL(&network_mux);network.setup=enabled;portEXIT_CRITICAL(&network_mux);}
    memset(&ap,0,sizeof(ap));
}
/* Stay on a healthy SSID. Offline: prefer a visible saved SSID, cool down failed
   profiles and rotate even when a stronger AP accepts association but gives no IP. */
static int choose_profile(const saved_wifi_t *config,int64_t now) {
    tracker_wifi_candidate_t candidates[PROFILE_COUNT]={0};
    portENTER_CRITICAL(&network_mux);
    for(unsigned i=0;i<PROFILE_COUNT;i++){
        candidates[i]=(tracker_wifi_candidate_t){.configured=config->profiles[i].ssid[0]!=0,.rssi=-1000,.tried_ms=tried_at[i]};
        for(unsigned j=0;j<scan_count;j++)if(!strcmp(config->profiles[i].ssid,scan_entries[j].ssid)){candidates[i].rssi=scan_entries[j].rssi;break;}
    }
    int locked=locked_profile;
    portEXIT_CRITICAL(&network_mux);return tracker_wifi_choose(candidates,PROFILE_COUNT,locked,now);
}
static void connect_profile(const saved_wifi_t *config,int index,int64_t now) {
    wifi_config_t station={0};strcpy((char *)station.sta.ssid,config->profiles[index].ssid);strcpy((char *)station.sta.password,config->profiles[index].password);station_defaults(&station,station.sta.password[0]!=0);
    esp_wifi_disconnect();esp_wifi_set_config(WIFI_IF_STA,&station);
    portENTER_CRITICAL(&network_mux);current_profile=index;strcpy(network.ssid,config->profiles[index].ssid);tried_at[index]=now;portEXIT_CRITICAL(&network_mux);
    esp_wifi_connect();attempt_started=now;next_connect=now+15000;memset(&station,0,sizeof(station));
}
bool tracker_web_start(void) {
    status_mutex=xSemaphoreCreateMutex();if(!status_mutex)return false;
    esp_err_t err=esp_netif_init();if(err!=ESP_OK)return false;
    err=esp_event_loop_create_default();if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE)return false;
    if(!esp_netif_create_default_wifi_sta()||!esp_netif_create_default_wifi_ap())return false;
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    err=esp_wifi_init(&init);if(err!=ESP_OK)return false;
    esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL);
    esp_event_handler_register(IP_EVENT,ESP_EVENT_ANY_ID,wifi_event,NULL);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    tracker_internet_init();load_wifi();network.configured=any_profile(&saved);network.setup=!network.configured;
    gpio_config_t button={.pin_bit_mask=1ULL<<0,.mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE};gpio_config(&button);
    err=esp_wifi_set_mode(WIFI_MODE_APSTA);
    if(err==ESP_OK){apply_hotspot(&saved,network.setup);err=esp_wifi_start();}
    outage_started=esp_timer_get_time()/1000;
    if(err!=ESP_OK){ESP_LOGE(TAG,"WLAN start failed: %s",esp_err_to_name(err));return false;}
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.stack_size=8192;config.lru_purge_enable=true;
    config.max_open_sockets=4;config.recv_wait_timeout=15;config.send_wait_timeout=5;config.max_uri_handlers=48;
    httpd_handle_t server=NULL;err=httpd_start(&server,&config);if(err!=ESP_OK)return false;
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=index_get},
        {.uri="/i18n.js",.method=HTTP_GET,.handler=i18n_get},
        {.uri="/decoder.js",.method=HTTP_GET,.handler=decoder_get},
        {.uri="/network.js",.method=HTTP_GET,.handler=network_get},
        {.uri="/satellite-catalog.js",.method=HTTP_GET,.handler=catalog_get},
        {.uri="/api/status",.method=HTTP_GET,.handler=status_get},
        {.uri="/timezones.js",.method=HTTP_GET,.handler=timezones_get},
        {.uri="/dashboard.js",.method=HTTP_GET,.handler=dashboard_get},
        {.uri="/recording.js",.method=HTTP_GET,.handler=recording_get},
        {.uri="/history-cache.js",.method=HTTP_GET,.handler=history_cache_get},
        {.uri="/extras.js",.method=HTTP_GET,.handler=extras_get},
        {.uri="/orbits.js",.method=HTTP_GET,.handler=orbit_get},
        {.uri="/firmware.js",.method=HTTP_GET,.handler=firmware_get},
        {.uri="/backup.js",.method=HTTP_GET,.handler=backup_get},
        {.uri="/adr.js",.method=HTTP_GET,.handler=adr_get},
        {.uri="/credentials.js",.method=HTTP_GET,.handler=credentials_get},
        {.uri="/display.js",.method=HTTP_GET,.handler=display_get},
        {.uri="/satellite.js",.method=HTTP_GET,.handler=satellite_js_get},
        {.uri="/earth-land.json",.method=HTTP_GET,.handler=earth_land_get},
        {.uri="/satellite.LICENSE.txt",.method=HTTP_GET,.handler=satellite_license_get},
        {.uri="/api/wifi",.method=HTTP_POST,.handler=configure_post},
        {.uri="/api/wifi/scan",.method=HTTP_GET,.handler=scan_get},
        {.uri="/api/wifi/scan",.method=HTTP_POST,.handler=scan_post}
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));
    tracker_ota_register(server);
    tracker_display_register(server);
    tracker_radio_register(server);tracker_internet_register(server);
    tracker_credentials_register(server);
    tracker_backup_register(server);
    tracker_log_register(server);
    tracker_mqtt_init();
    tracker_mqtt_register(server);
    satellite_log_register(server);
    import_log_register(server);
    gnss_diagnostics_register(server);
    running=true;
    if(network.setup)ESP_LOGI(TAG,"Setup WLAN %s ready, http://192.168.4.1",TRACKER_SETUP_SSID);
    else ESP_LOGI(TAG,"Connecting to saved home WLAN");
    return true;
}
void tracker_web_poll(void) {
    if(!running)return;
    tracker_ota_poll();int64_t now=esp_timer_get_time()/1000;network_state_t state=network_copy();saved_wifi_t config=saved_copy();
    display_button_event_t event=display_button_update(&user_button,!gpio_get_level(GPIO_NUM_0),now,tracker_display_is_on());
    if(event==DISPLAY_BUTTON_WAKE||event==DISPLAY_BUTTON_NEXT||event==DISPLAY_BUTTON_HOME)tracker_display_button(event);
    if(event==DISPLAY_BUTTON_HOTSPOT){suppress_hotspot=false;manual_hotspot=true;apply_hotspot(&config,true);ESP_LOGI(TAG,"USER held 3 seconds: hotspot enabled until reboot or manual close");}
    int target=-1;bool change=false,ap_change=false,enable=false,disable=false,automatic=false;
    portENTER_CRITICAL(&network_mux);
    if(now>=network.apply_at_ms){change=switch_pending;target=pending_profile;switch_pending=false;ap_change=ap_pending;ap_pending=false;enable=hotspot_pending;hotspot_pending=false;disable=hotspot_off_pending;hotspot_off_pending=false;automatic=hotspot_auto_pending;hotspot_auto_pending=false;}
    int64_t outage=outage_started;
    portEXIT_CRITICAL(&network_mux);
    if(enable){manual_hotspot=true;suppress_hotspot=false;}
    if(disable){manual_hotspot=false;suppress_hotspot=true;apply_hotspot(&config,false);}
    else if(automatic){manual_hotspot=false;suppress_hotspot=false;}
    else if(ap_change||enable)apply_hotspot(&config,state.setup||enable);
    if(change){
        esp_wifi_disconnect();attempt_started=0;next_connect=now;memset(tried_at,0,sizeof(tried_at));
        portENTER_CRITICAL(&network_mux);network.connected=false;network.ip[0]=0;current_profile=-1;if(!outage_started)outage_started=now;portEXIT_CRITICAL(&network_mux);
        if(target>=0&&target<PROFILE_COUNT&&config.profiles[target].ssid[0])connect_profile(&config,target,now);
    } else if(!state.connected&&network.configured&&now>=next_connect){
        int next=choose_profile(&config,now);
        if(next>=0)connect_profile(&config,next,now);
        else {next_connect=now+1000;if(attempt_started){esp_wifi_disconnect();attempt_started=0;}}
    }
    /* No IP counts as offline, including successful association without DHCP. */
    state=network_copy();
    bool hotspot_on=tracker_wifi_hotspot_on(suppress_hotspot?2:manual_hotspot?1:0,state.configured,state.connected,state.setup,state.connected_ms,outage,now);
    if(hotspot_on!=state.setup)apply_hotspot(&config,hotspot_on);
    if(!state.connected&&now>=next_auto_scan){
        portENTER_CRITICAL(&network_mux);bool busy=scan_busy;portEXIT_CRITICAL(&network_mux);
        if(!busy){wifi_scan_config_t scan={0};esp_err_t err=esp_wifi_scan_start(&scan,false);if(err==ESP_OK){portENTER_CRITICAL(&network_mux);scan_busy=true;portEXIT_CRITICAL(&network_mux);}}
        next_auto_scan=now+30000;
    }
    tracker_internet_poll(state.connected);
    memset(&config,0,sizeof(config));
}
static tracker_message_log_t message_log;
void tracker_web_message_queued(uint8_t port,const uint8_t *payload,size_t length,int64_t now_ms){tracker_message_queue(&message_log,port,payload,length,now_ms);}
void tracker_web_message_complete(bool success,int64_t now_ms){lorawan_uplink_info_t info={0};lorawan_port_get_last_uplink(&info);tracker_message_complete(&message_log,success,now_ms,info.counter);}
static void message_json(cJSON *parent,const char *name,const tracker_message_t *message,bool completed){
 if(!message->valid)return;
 cJSON *j=cJSON_AddObjectToObject(parent,name);if(!j)return;
 cJSON_AddNumberToObject(j,"fport",message->fport);cJSON_AddNumberToObject(j,"length",message->length);cJSON_AddNumberToObject(j,"queued_uptime_ms",(double)message->queued_ms);
 if(completed){cJSON_AddBoolToObject(j,"success",message->success);cJSON_AddNumberToObject(j,"completed_uptime_ms",(double)message->completed_ms);cJSON_AddNumberToObject(j,"counter",message->counter);}
 cJSON *bytes=cJSON_AddArrayToObject(j,"bytes");for(unsigned i=0;bytes&&i<message->length;i++)cJSON_AddItemToArray(bytes,cJSON_CreateNumber(message->payload[i]));
}
/* Main-task scheduling metadata only; this does not change RF/MAC timing. */
static struct {int64_t queued,completed,next,join;uint8_t last_port,next_port;} lora_timing;
void tracker_web_lora_timing(int64_t queued_ms,int64_t completed_ms,uint8_t last_port,int64_t next_ms,uint8_t next_port,int64_t join_ms){
 lora_timing.queued=queued_ms;lora_timing.completed=completed_ms;lora_timing.last_port=last_port;lora_timing.next=next_ms;lora_timing.next_port=next_port;lora_timing.join=join_ms;
}
void tracker_web_publish(const tracker_gnss_t *g,int64_t now,bool joined) {
    if(!running)return;
    network_state_t state=network_copy();uint8_t bytes[51];
    tracker_position_payload(g,now,0,bytes);tracker_sat_snapshot(g,now,0,&web_satellites);
    cJSON *root=cJSON_CreateObject();if(!root)return;
    cJSON_AddBoolToObject(root,"lora_session",joined);
    cJSON_AddNumberToObject(root,"gnss_sentences",g->sentences);
    cJSON_AddNumberToObject(root,"checksum_errors",g->checksum_errors);
    cJSON *clock=cJSON_AddObjectToObject(root,"gnss_time");
    if(clock){bool fresh=g->utc_received_ms>=0&&now>=g->utc_received_ms&&now-g->utc_received_ms<10000;cJSON_AddBoolToObject(clock,"recent",fresh);cJSON_AddBoolToObject(clock,"confirmed",fresh&&g->utc_confirmed);if(fresh)cJSON_AddNumberToObject(clock,"utc_ms",(double)(g->utc_epoch_ms+now-g->utc_received_ms));}
    cJSON *measurement=cJSON_AddObjectToObject(root,"measurement_time");
    if(measurement){int64_t position=tracker_gnss_position_utc(g,now);if(position>=0)cJSON_AddNumberToObject(measurement,"position_utc_ms",(double)position);else cJSON_AddNullToObject(measurement,"position_utc_ms");cJSON_AddNumberToObject(measurement,"uptime_ms",(double)now);}
    cJSON *receiver=cJSON_AddObjectToObject(root,"gnss_receiver");if(receiver){cJSON_AddBoolToObject(receiver,"configuration_known",g->configuration_known);if(g->configuration_known)cJSON_AddNumberToObject(receiver,"mask",g->configuration_mask);}
    cJSON *diag=cJSON_AddObjectToObject(root,"diagnostics");
    if(diag){cJSON_AddNumberToObject(diag,"reset_reason",esp_reset_reason());cJSON_AddNumberToObject(diag,"uptime_ms",(double)now);
        if(g->nmea_ms>=0&&now>=g->nmea_ms)cJSON_AddNumberToObject(diag,"gnss_age_ms",(double)(now-g->nmea_ms));else cJSON_AddNullToObject(diag,"gnss_age_ms");
        cJSON_AddNumberToObject(diag,"main_stack_min_free_bytes",uxTaskGetStackHighWaterMark(NULL));
        if(state.http_stack_min_free)cJSON_AddNumberToObject(diag,"http_stack_min_free_bytes",state.http_stack_min_free);
        cJSON_AddNumberToObject(diag,"status_build_failures",state.status_build_failures);cJSON_AddNumberToObject(diag,"status_published_bytes",state.status_published_bytes);cJSON_AddBoolToObject(diag,"satellite_overflow",g->overflow);}
    tracker_ota_json(root);
    tracker_backup_json(root);
    tracker_display_json(root);
    tracker_log_json(root);
    satellite_log_json(root);
    import_log_json(root);
    cJSON *accuracy=cJSON_AddObjectToObject(root,"accuracy");
    if(accuracy){bool fresh=tracker_gnss_has_fix(g,now)&&g->gst_ms>=0&&now>=g->gst_ms&&now-g->gst_ms<3000&&isfinite(g->sigma_lat)&&isfinite(g->sigma_lon);cJSON_AddBoolToObject(accuracy,"available",fresh);cJSON_AddStringToObject(accuracy,"source","GST");if(fresh){cJSON_AddNumberToObject(accuracy,"north_sigma_m",g->sigma_lat);cJSON_AddNumberToObject(accuracy,"east_sigma_m",g->sigma_lon);cJSON_AddNumberToObject(accuracy,"horizontal_rms_m",hypot(g->sigma_lat,g->sigma_lon));if(isfinite(g->sigma_alt))cJSON_AddNumberToObject(accuracy,"vertical_sigma_m",g->sigma_alt);}}

    tracker_mqtt_snapshot(g,now,joined);
    tracker_mqtt_json(root);tracker_internet_json(root);
    cJSON *lora=cJSON_AddObjectToObject(root,"lora");
    if(lora) {
        tracker_radio_json(lora);
        tracker_credentials_json(lora);
        tracker_adr_json(lora);
        message_json(lora,"last_message",&message_log.last,true);message_json(lora,"pending_message",&message_log.pending,false);
        uint32_t mac=LoRaMacState;const char *phase=(mac&LORAMAC_TX_DELAYED)?"deferred":(mac&LORAMAC_CAD_RUNNING)?"channel_check":(mac&LORAMAC_TX_RUNNING)?"transaction":(mac&LORAMAC_TX_CONFIG)?"preparing":"ready";
        cJSON_AddStringToObject(lora,"radio_phase",phase);

        cJSON *timing=cJSON_AddObjectToObject(lora,"timing");if(timing){
            if(lora_timing.queued>0)cJSON_AddNumberToObject(timing,"last_queued_uptime_ms",(double)lora_timing.queued);
            if(lora_timing.completed>0)cJSON_AddNumberToObject(timing,"last_completed_uptime_ms",(double)lora_timing.completed);
            cJSON_AddNumberToObject(timing,"last_fport",lora_timing.last_port);cJSON_AddNumberToObject(timing,"next_fport",lora_timing.next_port);
            cJSON_AddNumberToObject(timing,"next_planned_uptime_ms",(double)lora_timing.next);cJSON_AddNumberToObject(timing,"next_join_uptime_ms",(double)lora_timing.join);
        }
        cJSON_AddBoolToObject(lora,"joined",joined);cJSON_AddBoolToObject(lora,"join_pending",lorawan_port_is_join_pending());
        cJSON_AddBoolToObject(lora,"tx_pending",lorawan_port_is_tx_pending());
        bool adr=false;lorawan_port_get_adr(&adr);cJSON_AddBoolToObject(lora,"adr",adr);
        /* The port getter is the optional override (-1 = automatic), not active MAC DR. */
        MibRequestConfirm_t data_rate={.Type=MIB_CHANNELS_DATARATE};
        int8_t dr=0;if(LoRaMacMibGetRequestConfirm(&data_rate)==LORAMAC_STATUS_OK)dr=data_rate.Param.ChannelsDatarate;
        uint8_t sf=0;uint16_t bw=0;bool fsk=false;
        lorawan_port_get_dr_params(dr,&sf,&bw,&fsk);cJSON_AddNumberToObject(lora,"dr",dr);cJSON_AddNumberToObject(lora,"sf",sf);cJSON_AddNumberToObject(lora,"bandwidth_khz",bw);
        int8_t current_power;uint32_t adr_ack;LoRaMacGetAdrState(&current_power,&adr_ack);cJSON_AddNumberToObject(lora,"current_power_dbm",current_power);cJSON_AddNumberToObject(lora,"adr_ack_count",adr_ack);
        LoRaMacRegionalState_t receive;LoRaMacGetRegionalState(&receive);
        cJSON *rx=cJSON_AddObjectToObject(lora,"rx_windows");if(rx){
            cJSON_AddNumberToObject(rx,"rx1_delay_ms",receive.receive_delay1);
            cJSON_AddNumberToObject(rx,"rx2_delay_ms",receive.receive_delay2);
            cJSON_AddNumberToObject(rx,"rx1_dr_offset",receive.rx1_offset);
            MibRequestConfirm_t rx2={.Type=MIB_RX2_CHANNEL};
            if(LoRaMacMibGetRequestConfirm(&rx2)==LORAMAC_STATUS_OK){cJSON_AddNumberToObject(rx,"rx2_frequency_hz",rx2.Param.Rx2Channel.Frequency);cJSON_AddNumberToObject(rx,"rx2_dr",rx2.Param.Rx2Channel.Datarate);}
        }
        sx1262_tx_power_t power;if(sx1262_get_last_tx_power(&power)){cJSON *tx=cJSON_AddObjectToObject(lora,"tx_power");if(tx){cJSON_AddNumberToObject(tx,"target_dbm",power.target_dbm);cJSON_AddNumberToObject(tx,"target_mw",pow(10.0,power.target_dbm/10.0));cJSON_AddNumberToObject(tx,"requested_dbm",power.requested_dbm);cJSON_AddNumberToObject(tx,"sx1262_drive_dbm",power.drive_dbm);cJSON_AddBoolToObject(tx,"measured",false);}}
        MibRequestConfirm_t mib={.Type=MIB_DEV_ADDR};if(LoRaMacMibGetRequestConfirm(&mib)==LORAMAC_STATUS_OK){char address[9];snprintf(address,sizeof(address),"%08lX",(unsigned long)mib.Param.DevAddr);cJSON_AddStringToObject(lora,"dev_addr",address);}
        lorawan_uplink_info_t up;if(lorawan_port_get_last_uplink(&up)&&up.valid) {
            cJSON *u=cJSON_AddObjectToObject(lora,"last_uplink");if(u){cJSON_AddBoolToObject(u,"local_success",up.success);cJSON_AddNumberToObject(u,"frame_counter",up.counter);cJSON_AddNumberToObject(u,"airtime_ms",up.toa_ms);cJSON_AddNumberToObject(u,"channel",up.channel);cJSON_AddNumberToObject(u,"power_index",up.tx_power);cJSON_AddNumberToObject(u,"retries",up.retries);}
            MibRequestConfirm_t channels={.Type=MIB_CHANNELS};
            if(u&&up.channel<16&&LoRaMacMibGetRequestConfirm(&channels)==LORAMAC_STATUS_OK&&channels.Param.ChannelList)
                cJSON_AddNumberToObject(u,"frequency_hz",channels.Param.ChannelList[up.channel].Frequency);
        }
        lorawan_downlink_info_t down;if(lorawan_port_get_last_downlink(&down)&&down.valid) {
            cJSON *d=cJSON_AddObjectToObject(lora,"last_downlink");if(d){cJSON_AddNumberToObject(d,"rssi_dbm",down.rssi);cJSON_AddNumberToObject(d,"snr_db",down.snr);cJSON_AddNumberToObject(d,"frame_counter",down.counter);cJSON_AddBoolToObject(d,"ack",down.ack);cJSON_AddNumberToObject(d,"fport",down.port);cJSON_AddNumberToObject(d,"length",down.len);}
        }
    }
    saved_wifi_t config=saved_copy();
    cJSON *wifi=cJSON_AddObjectToObject(root,"wifi");
    if(wifi) {
        cJSON_AddStringToObject(wifi,"state",state.connected?"connected":state.configured?"connecting":"setup");
        cJSON_AddStringToObject(wifi,"ssid",state.ssid);cJSON_AddStringToObject(wifi,"ip",state.ip);
        cJSON_AddBoolToObject(wifi,"setup_active",state.setup);cJSON_AddNumberToObject(wifi,"last_reason",state.last_reason);
        cJSON_AddBoolToObject(wifi,"bssid_pinned",false);cJSON_AddNumberToObject(wifi,"disconnects",state.disconnects);
        portENTER_CRITICAL(&network_mux);int active=current_profile,locked=locked_profile;bool manual=manual_hotspot;portEXIT_CRITICAL(&network_mux);
        cJSON_AddNumberToObject(wifi,"current_profile",active);cJSON_AddBoolToObject(wifi,"locked_until_reboot",locked>=0);
        cJSON_AddStringToObject(wifi,"hotspot_ssid",config.ap_ssid);cJSON_AddBoolToObject(wifi,"hotspot_open",!config.ap_password[0]);cJSON_AddBoolToObject(wifi,"hotspot_manual",manual);cJSON_AddStringToObject(wifi,"hotspot_mode",suppress_hotspot?"off":manual?"on":"auto");
        cJSON *profiles=cJSON_AddArrayToObject(wifi,"profiles");
        for(unsigned i=0;profiles&&i<PROFILE_COUNT;i++)if(config.profiles[i].ssid[0]){cJSON *item=cJSON_CreateObject();if(!item)continue;cJSON_AddNumberToObject(item,"id",i);cJSON_AddStringToObject(item,"ssid",config.profiles[i].ssid);cJSON_AddBoolToObject(item,"secure",config.profiles[i].password[0]!=0);cJSON_AddItemToArray(profiles,item);}

        uint8_t mac[6];if(esp_wifi_get_mac(WIFI_IF_STA,mac)==ESP_OK){char value[18];snprintf(value,sizeof(value),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);cJSON_AddStringToObject(wifi,"mac",value);}
        cJSON_AddNumberToObject(wifi,"connected_seconds",state.connected?(now-state.connected_ms)/1000:0);
        wifi_ap_record_t ap;if(state.connected&&esp_wifi_sta_get_ap_info(&ap)==ESP_OK){char bssid[18];snprintf(bssid,sizeof(bssid),"%02X:%02X:%02X:%02X:%02X:%02X",ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5]);cJSON_AddStringToObject(wifi,"bssid",bssid);cJSON_AddNumberToObject(wifi,"channel",ap.primary);cJSON_AddNumberToObject(wifi,"rssi_dbm",ap.rssi);cJSON_AddNumberToObject(wifi,"auth_mode",ap.authmode);}
        esp_netif_t *iface=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");esp_netif_ip_info_t info;
        if(state.connected&&iface&&esp_netif_get_ip_info(iface,&info)==ESP_OK){char value[16];snprintf(value,sizeof(value),IPSTR,IP2STR(&info.netmask));cJSON_AddStringToObject(wifi,"netmask",value);snprintf(value,sizeof(value),IPSTR,IP2STR(&info.gw));cJSON_AddStringToObject(wifi,"gateway",value);}
        cJSON *dns=cJSON_AddArrayToObject(wifi,"dns");
        for(int i=0;state.connected&&iface&&dns&&i<2;i++){esp_netif_dns_info_t entry;if(esp_netif_get_dns_info(iface,i==0?ESP_NETIF_DNS_MAIN:ESP_NETIF_DNS_BACKUP,&entry)==ESP_OK&&entry.ip.type==ESP_IPADDR_TYPE_V4&&entry.ip.u_addr.ip4.addr){char value[16];snprintf(value,sizeof(value),IPSTR,IP2STR(&entry.ip.u_addr.ip4));cJSON_AddItemToArray(dns,cJSON_CreateString(value));}}

    }
    tracker_display_services(root);
    char *metadata=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    status_blob_t *next=metadata?status_build(metadata,bytes,&web_satellites):NULL;free(metadata);
    if(next&&xSemaphoreTake(status_mutex,0)==pdTRUE){
        size_t length=next->length;status_replace(&status_cache,next);xSemaphoreGive(status_mutex);
        portENTER_CRITICAL(&network_mux);network.status_published_bytes=length;portEXIT_CRITICAL(&network_mux);
    }else{status_release(next);portENTER_CRITICAL(&network_mux);network.status_build_failures++;portEXIT_CRITICAL(&network_mux);}
    memset(&state.station,0,sizeof(state.station));memset(&config,0,sizeof(config));
}

