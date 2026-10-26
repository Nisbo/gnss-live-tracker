"""Exercise production MQTT password/configuration and disabled-snapshot paths."""
from pathlib import Path
import subprocess, re
from test_support import ROOT, CJSON, CC, tmp
s=(ROOT/'main/tracker_mqtt.c').read_text()
def section(start,end):return s[s.index(start):s.index(end,s.index(start))]
config=re.search(r'typedef struct \{uint32_t version;.*?\} config_t;',s).group()
sample=re.search(r'typedef struct \{int64_t at,utc;.*?\} sample_t;',s).group()
code=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "esp_http_server.h"
#include "nvs.h"
#include "cJSON.h"
#include "mqtt_policy.h"
#include "gnss_nmea.h"
#define portMAX_DELAY 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
static int lock=1,samples=1;
static void xSemaphoreTake(int l,int t){(void)t;assert(l);}
static void xSemaphoreGive(int l){assert(l);}
'''+config+'\n'+sample+r'''
static config_t config,stored;static unsigned revision=1,snapshots,writes,wifi_queries;static bool denied,commit_fail,no_store;
static bool tracker_request_authorized(httpd_req_t *r){(void)r;return !denied;}
static int httpd_req_recv(httpd_req_t *r,char *b,size_t n){if(n>r->content_len-r->received)n=r->content_len-r->received;memcpy(b,r->body+r->received,n);r->received+=n;return n;}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *s){(void)s;r->status=status;return ESP_FAIL;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;assert(!strcmp(s,"application/json"));return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;assert(!strcmp(k,"Cache-Control")&&!strcmp(v,"no-store"));no_store=true;return ESP_OK;}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){snprintf(r->response,sizeof(r->response),"%s",s);return ESP_OK;}
esp_err_t nvs_open(const char *s,int mode,int *h){assert(!strcmp(s,"tracker_mqtt")&&mode==NVS_READWRITE);*h=1;return ESP_OK;}
void nvs_close(int h){(void)h;}esp_err_t nvs_commit(int h){(void)h;return commit_fail?ESP_FAIL:ESP_OK;}
esp_err_t nvs_set_blob(int h,const char *k,const void *p,size_t n){(void)h;assert(!strcmp(k,"config")&&n==sizeof(stored));memcpy(&stored,p,n);return ESP_OK;}
static bool tracker_gnss_has_fix(const tracker_gnss_t *g,int64_t now){(void)g;(void)now;return false;}
static void tracker_sat_snapshot(const tracker_gnss_t *g,int64_t now,uint16_t sequence,tracker_sat_snapshot_t *out){(void)g;(void)now;(void)sequence;memset(out,0,sizeof(*out));snapshots++;}
typedef struct {int rssi;} wifi_ap_record_t;
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){wifi_queries++;ap->rssi=-50;return ESP_OK;}
static void xQueueOverwrite(int q,const sample_t *p){assert(q&&p);writes++;}
'''
# Declarations from gnss_nmea.h already have external linkage.
code=code.replace('static bool tracker_gnss_has_fix','bool tracker_gnss_has_fix').replace('static void tracker_sat_snapshot','void tracker_sat_snapshot').replace('static int httpd_req_recv','int httpd_req_recv')
code+=section('static config_t copy_config','static bool send_json')
code+=section('void tracker_mqtt_snapshot','void tracker_mqtt_json')
code+=section('static esp_err_t reveal_password','void tracker_mqtt_register')
code+=r'''
static httpd_req_t post(const char *body){httpd_req_t req={.body=body,.content_len=strlen(body),.status=200};configure(&req);return req;}
int main(void){
 config=(config_t){.version=1,.port=1883,.interval=10,.discovery=true};strcpy(config.topic,"tracker/example");strcpy(config.password,"synthetic-\"password");
 config_t before=config;denied=true;assert(post("{\"action\":\"reveal\"}").status==403);denied=false;
 httpd_req_t r=post("{\"action\":\"reveal\"}");assert(r.status==200&&no_store&&revision==1&&!memcmp(&before,&config,sizeof(config)));cJSON *reply=cJSON_Parse(r.response);assert(!strcmp(cJSON_GetObjectItem(reply,"password")->valuestring,config.password));cJSON_Delete(reply);
 assert(post("{\"port\":18884}").status==200&&!strcmp(config.password,before.password)&&config.port==18884);
 assert(post("{\"password\":\"\"}").status==200&&!config.password[0]);
 assert(post("{\"password\":\"new-example-password\",\"username\":\"\"}").status==200&&!strcmp(config.password,"new-example-password"));
 before=config;assert(post("{\"password\":false}").status==400&&!memcmp(&before,&config,sizeof(config)));
 assert(post("{\"action\":\"unknown\"}").status==400);commit_fail=true;assert(post("{\"password\":\"different\"}").status==500&&!memcmp(&before,&config,sizeof(config)));commit_fail=false;
 tracker_gnss_t g={0};tracker_mqtt_snapshot(&g,1000,false);assert(!snapshots&&!writes&&!wifi_queries);config.enabled=true;tracker_mqtt_snapshot(&g,1000,false);assert(snapshots==1&&writes==1&&wifi_queries==1);
 puts("Production MQTT: authorized no-cache password reveal, masked-edit contract, empty removal, retained omitted password, custom ports, failed-save isolation and disabled snapshot optimization PASS");
}
'''
path=Path(tmp('mqtt-server.c'));path.write_text(code);binary=tmp('mqtt-server')
subprocess.run([CC,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'main'),'-I'+str(ROOT/'tests/backup_stubs'),'-I'+CJSON,str(path),str(Path(CJSON,'cJSON.c')),'-lm','-o',binary],check=True)
subprocess.run([binary],check=True)
