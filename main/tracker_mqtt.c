#include "tracker_mqtt.h"
#include "tracker_web.h"
#include "tracker_ota.h"
#include "tracker_log.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "mqtt_policy.h"

typedef struct {uint32_t version;bool enabled,discovery,commands;uint16_t port,interval;char host[128],username[65],password[129],topic[96];} config_t;
bool tracker_mqtt_backup_valid(const void *data,size_t n){if(n!=sizeof(config_t))return false;config_t c;memcpy(&c,data,n);bool valid=c.version==1&&memchr(c.host,0,sizeof(c.host))&&memchr(c.username,0,sizeof(c.username))&&memchr(c.password,0,sizeof(c.password))&&memchr(c.topic,0,sizeof(c.topic));if(valid)valid=mqtt_settings_valid(c.host,c.topic,c.port,c.interval,c.enabled);memset(&c,0,sizeof(c));return valid;}
typedef struct {int64_t at,utc;double lat,lon,alt;bool fix,joined;uint8_t used,visible[7],system_used[7];int rssi;} sample_t;
typedef struct {char json[160];} command_t;
static config_t config;
static SemaphoreHandle_t lock;
static QueueHandle_t samples,commands;
static unsigned revision;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static bool connected,started,announce;
static unsigned reconnects,queued,command_count;
static int last_error;
static char identity[40];
static config_t copy_config(unsigned *rev){config_t c={0};xSemaphoreTake(lock,portMAX_DELAY);c=config;if(rev)*rev=revision;xSemaphoreGive(lock);return c;}
static bool send_json(esp_mqtt_client_handle_t client,const char *topic,const char *json,bool retain){/* Network calls run only in this worker, never in GNSS/HTTP. QoS 0 avoids an offline telemetry backlog. */
 return esp_mqtt_client_publish(client,topic,json,0,0,retain)>=0;}
static void event(void *arg,esp_event_base_t base,int32_t id,void *data){
 esp_mqtt_event_handle_t e=data;
 portENTER_CRITICAL(&mux);
 if(id==MQTT_EVENT_CONNECTED){connected=true;announce=true;reconnects++;last_error=0;}
 if(id==MQTT_EVENT_DISCONNECTED)connected=false;
 if(id==MQTT_EVENT_ERROR){last_error=e->error_handle?e->error_handle->error_type:-1;}
 portEXIT_CRITICAL(&mux);
 if(id==MQTT_EVENT_DATA&&e->topic_len==20&&!memcmp(e->topic,"homeassistant/status",20)&&e->data_len==6&&!memcmp(e->data,"online",6)){portENTER_CRITICAL(&mux);announce=true;portEXIT_CRITICAL(&mux);}
 if(id==MQTT_EVENT_DATA&&e->data_len==e->total_data_len&&e->current_data_offset==0&&!e->retain&&e->data_len>0&&e->data_len<sizeof(((command_t *)0)->json)){
  /* Only this exact topic is accepted; fragmented or retained commands are ignored. */
  const char *expected=arg;if(e->topic_len==(int)strlen(expected)&&!memcmp(e->topic,expected,e->topic_len)){command_t c={0};memcpy(c.json,e->data,e->data_len);xQueueSend(commands,&c,0);}
 }
}
static void discovery(esp_mqtt_client_handle_t client,const config_t *c){
 const char *keys[]={"fix","satellites_used","wifi_rssi","uptime_s","lora_joined"};
 const char *names[]={"GNSS Fix","Satelliten im Fix","WLAN Signal","Laufzeit","LoRaWAN Session"};
 for(unsigned i=0;i<5;i++){
  char topic[160],state[128],available[128],unique[80],value[100];
  snprintf(topic,sizeof(topic),"homeassistant/sensor/%s/%s/config",identity,keys[i]);snprintf(state,sizeof(state),"%s/state",c->topic);snprintf(available,sizeof(available),"%s/availability",c->topic);snprintf(unique,sizeof(unique),"%s_%s",identity,keys[i]);snprintf(value,sizeof(value),"{{ value_json.%s }}",keys[i]);
  cJSON *j=cJSON_CreateObject();if(!j)continue;cJSON_AddStringToObject(j,"name",names[i]);cJSON_AddStringToObject(j,"unique_id",unique);cJSON_AddStringToObject(j,"state_topic",state);cJSON_AddStringToObject(j,"value_template",value);cJSON_AddStringToObject(j,"availability_topic",available);
  if(i==2){cJSON_AddStringToObject(j,"unit_of_measurement","dBm");cJSON_AddStringToObject(j,"device_class","signal_strength");}if(i==3)cJSON_AddStringToObject(j,"unit_of_measurement","s");
  cJSON *device=cJSON_AddObjectToObject(j,"device");cJSON *ids=cJSON_AddArrayToObject(device,"identifiers");cJSON_AddItemToArray(ids,cJSON_CreateString(identity));cJSON_AddStringToObject(device,"name","Heltec Tracker V2");cJSON_AddStringToObject(device,"manufacturer","Heltec");cJSON_AddStringToObject(device,"model","Wireless Tracker V2");cJSON_AddStringToObject(device,"sw_version",esp_app_get_description()->version);
  char *body=cJSON_PrintUnformatted(j);if(body){send_json(client,topic,body,true);free(body);}cJSON_Delete(j);
 }
 char topic[160],state[128],available[128],unique[80];snprintf(topic,sizeof(topic),"homeassistant/device_tracker/%s/position/config",identity);snprintf(state,sizeof(state),"%s/position",c->topic);snprintf(available,sizeof(available),"%s/availability",c->topic);snprintf(unique,sizeof(unique),"%s_position",identity);
 cJSON *j=cJSON_CreateObject();if(j){cJSON_AddStringToObject(j,"name","Position");cJSON_AddStringToObject(j,"unique_id",unique);cJSON_AddStringToObject(j,"json_attributes_topic",state);cJSON_AddStringToObject(j,"source_type","gps");cJSON_AddStringToObject(j,"availability_topic",available);cJSON *d=cJSON_AddObjectToObject(j,"device");cJSON *ids=cJSON_AddArrayToObject(d,"identifiers");cJSON_AddItemToArray(ids,cJSON_CreateString(identity));char *body=cJSON_PrintUnformatted(j);if(body){send_json(client,topic,body,true);free(body);}cJSON_Delete(j);}
}
static void publish_sample(esp_mqtt_client_handle_t client,const config_t *c,const sample_t *s){
 cJSON *j=cJSON_CreateObject();if(!j)return;
 cJSON_AddBoolToObject(j,"fix",s->fix);cJSON_AddBoolToObject(j,"lora_joined",s->joined);cJSON_AddNumberToObject(j,"uptime_s",s->at/1000);cJSON_AddNumberToObject(j,"satellites_used",s->used);cJSON_AddNumberToObject(j,"wifi_rssi",s->rssi);
 if(s->utc)cJSON_AddNumberToObject(j,"utc_ms",(double)s->utc);
 if(s->fix){cJSON_AddNumberToObject(j,"latitude",s->lat);cJSON_AddNumberToObject(j,"longitude",s->lon);if(isfinite(s->alt))cJSON_AddNumberToObject(j,"altitude",s->alt);}else{cJSON_AddNullToObject(j,"latitude");cJSON_AddNullToObject(j,"longitude");}
 cJSON *systems=cJSON_AddObjectToObject(j,"systems");const char *names[]={"GPS","GLONASS","Galileo","BeiDou","QZSS","SBAS","NavIC"};for(int i=0;i<7;i++){cJSON *sys=cJSON_AddObjectToObject(systems,names[i]);if(s->visible[i]!=255)cJSON_AddNumberToObject(sys,"reported",s->visible[i]);else cJSON_AddNullToObject(sys,"reported");if(s->system_used[i]!=255)cJSON_AddNumberToObject(sys,"used",s->system_used[i]);else cJSON_AddNullToObject(sys,"used");}
 char topic[128];snprintf(topic,sizeof(topic),"%s/state",c->topic);char *body=cJSON_PrintUnformatted(j);if(body){if(send_json(client,topic,body,false)){portENTER_CRITICAL(&mux);queued++;portEXIT_CRITICAL(&mux);}free(body);}cJSON_Delete(j);
 /* HA retains the last known location when the receiver temporarily loses its fix. */
 if(s->fix){char pos[192];snprintf(pos,sizeof(pos),"{\"latitude\":%.7f,\"longitude\":%.7f,\"source_type\":\"gps\"}",s->lat,s->lon);snprintf(topic,sizeof(topic),"%s/position",c->topic);send_json(client,topic,pos,false);}
}
static void worker(void *unused){
 unsigned applied=0;esp_mqtt_client_handle_t client=NULL;config_t c={0};char availability[128],command_topic[128];int64_t next=0;sample_t sample={0};bool have_sample=false;
 for(;;){
  unsigned rev;config_t desired=copy_config(&rev);
  if(rev!=applied){
   if(client){if(connected)send_json(client,availability,"offline",true);esp_mqtt_client_stop(client);esp_mqtt_client_destroy(client);client=NULL;}
   portENTER_CRITICAL(&mux);connected=started=announce=false;portEXIT_CRITICAL(&mux);c=desired;applied=rev;next=0;xQueueReset(commands);
   if(c.enabled){
    snprintf(availability,sizeof(availability),"%s/availability",c.topic);snprintf(command_topic,sizeof(command_topic),"%s/command",c.topic);
    esp_mqtt_client_config_t options={.broker.address={.hostname=c.host,.port=c.port,.transport=MQTT_TRANSPORT_OVER_TCP},.credentials={.client_id=identity,.username=c.username[0]?c.username:NULL,.authentication.password=c.password[0]?c.password:NULL},.session={.keepalive=30,.last_will={.topic=availability,.msg="offline",.qos=0,.retain=1}},.network={.timeout_ms=3000,.reconnect_timeout_ms=15000},.task.stack_size=5120,.buffer.size=1024,.outbox.limit=8192};
    client=esp_mqtt_client_init(&options);if(client){esp_mqtt_client_register_event(client,ESP_EVENT_ANY_ID,event,command_topic);if(esp_mqtt_client_start(client)!=ESP_OK){esp_mqtt_client_destroy(client);client=NULL;}}
    portENTER_CRITICAL(&mux);started=client!=NULL;if(!client)last_error=-2;portEXIT_CRITICAL(&mux);
   }
  }
  memset(&desired,0,sizeof(desired));
  if(xQueueReceive(samples,&sample,0)==pdTRUE)have_sample=true;
  portENTER_CRITICAL(&mux);bool online=connected,hello=announce;announce=false;portEXIT_CRITICAL(&mux);
  if(client&&online){
   if(hello){if(c.commands)esp_mqtt_client_subscribe(client,command_topic,0);if(c.discovery){esp_mqtt_client_subscribe(client,"homeassistant/status",0);discovery(client,&c);}send_json(client,availability,"online",true);next=0;}
   command_t cmd;
   if(xQueueReceive(commands,&cmd,0)==pdTRUE&&c.commands){int enabled,interval;bool ok=mqtt_command_parse(cmd.json,&enabled,&interval);if(ok)ok=tracker_log_configure(enabled,interval);char reply[64],topic[128];snprintf(reply,sizeof(reply),"{\"ok\":%s}",ok?"true":"false");snprintf(topic,sizeof(topic),"%s/command_result",c.topic);send_json(client,topic,reply,false);portENTER_CRITICAL(&mux);command_count++;portEXIT_CRITICAL(&mux);}
   int64_t now=esp_timer_get_time()/1000;if(have_sample&&now-sample.at<10000&&now>=next){publish_sample(client,&c,&sample);next=now+(int64_t)c.interval*1000;}
  }else{xQueueReset(commands);}
  vTaskDelay(pdMS_TO_TICKS(250));
 }
}
bool tracker_mqtt_init(void){
 lock=xSemaphoreCreateMutex();samples=xQueueCreate(1,sizeof(sample_t));commands=xQueueCreate(4,sizeof(command_t));if(!lock||!samples||!commands)return false;
 uint8_t mac[6]={0};esp_wifi_get_mac(WIFI_IF_STA,mac);snprintf(identity,sizeof(identity),"tracker_v2_%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
 config=(config_t){.version=1,.port=1883,.interval=10,.discovery=true};snprintf(config.topic,sizeof(config.topic),"tracker/%s",identity);
 nvs_handle_t h;if(nvs_open("tracker_mqtt",NVS_READONLY,&h)==ESP_OK){config_t stored={0};size_t n=sizeof(stored);if(nvs_get_blob(h,"config",&stored,&n)==ESP_OK&&n==sizeof(stored)&&stored.version==1&&memchr(stored.host,0,sizeof(stored.host))&&memchr(stored.username,0,sizeof(stored.username))&&memchr(stored.password,0,sizeof(stored.password))&&memchr(stored.topic,0,sizeof(stored.topic))&&mqtt_settings_valid(stored.host,stored.topic,stored.port,stored.interval,stored.enabled))config=stored;memset(&stored,0,sizeof(stored));nvs_close(h);}revision=1;
 return xTaskCreate(worker,"tracker-mqtt",6144,NULL,3,NULL)==pdPASS;
}
void tracker_mqtt_snapshot(const tracker_gnss_t *g,int64_t now,bool joined){
 if(!samples)return;
 sample_t s={.at=now,.fix=tracker_gnss_has_fix(g,now),.lat=g->latitude,.lon=g->longitude,.alt=g->altitude,.used=g->used,.joined=joined};
 if(g->utc_confirmed&&g->utc_received_ms>=0&&now-g->utc_received_ms<10000)s.utc=g->utc_epoch_ms+now-g->utc_received_ms;
 tracker_sat_snapshot_t snap;tracker_sat_snapshot(g,now,0,&snap);for(unsigned i=0;i<snap.count;i++){tracker_sat_view_t *sat=&snap.satellites[i];if(sat->system>=1&&sat->system<=7){if(sat->flags&SAT_VISIBLE)s.visible[sat->system-1]++;if(sat->flags&SAT_USED)s.system_used[sat->system-1]++;}}
 for(unsigned i=0;i<7;i++){if(g->gsv_system_ms[i+1]<0||now-g->gsv_system_ms[i+1]>=15000)s.visible[i]=255;if(!(snap.usage_known_mask&(1<<i)))s.system_used[i]=255;}
 wifi_ap_record_t ap;if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)s.rssi=ap.rssi;
 xQueueOverwrite(samples,&s);
}
void tracker_mqtt_json(cJSON *root){
 if(!lock)return;
 config_t c=copy_config(NULL);cJSON *j=cJSON_AddObjectToObject(root,"mqtt");if(j){cJSON_AddBoolToObject(j,"enabled",c.enabled);cJSON_AddStringToObject(j,"host",c.host);cJSON_AddNumberToObject(j,"port",c.port);cJSON_AddNumberToObject(j,"interval_s",c.interval);cJSON_AddStringToObject(j,"username",c.username);cJSON_AddBoolToObject(j,"password_set",c.password[0]!=0);cJSON_AddStringToObject(j,"topic",c.topic);cJSON_AddBoolToObject(j,"discovery",c.discovery);cJSON_AddBoolToObject(j,"commands",c.commands);
 portENTER_CRITICAL(&mux);bool on=connected,run=started;unsigned count=queued,re=reconnects,cmd=command_count;int error=last_error;portEXIT_CRITICAL(&mux);cJSON_AddStringToObject(j,"state",!c.enabled?"disabled":on?"connected":run?"connecting":"starting");cJSON_AddNumberToObject(j,"sent_messages",count);cJSON_AddNumberToObject(j,"connections",re);cJSON_AddNumberToObject(j,"commands_received",cmd);cJSON_AddNumberToObject(j,"last_error",error);}memset(&c,0,sizeof(c));
}
static esp_err_t configure(httpd_req_t *req){
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Request token required");
 if(req->content_len<2||req->content_len>1024)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid size");
 char body[1025];int n=0;while(n<req->content_len){int got=httpd_req_recv(req,body+n,req->content_len-n);if(got<=0)return ESP_FAIL;n+=got;}body[n]=0;cJSON *j=cJSON_Parse(body);memset(body,0,sizeof(body));config_t c=copy_config(NULL);bool valid=cJSON_IsObject(j);
 const char *strings[]={"host","username","password","topic"};char *targets[]={c.host,c.username,c.password,c.topic};size_t lengths[]={sizeof(c.host),sizeof(c.username),sizeof(c.password),sizeof(c.topic)};
 for(unsigned i=0;i<4;i++){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,strings[i]);if(v){if(!cJSON_IsString(v)||strlen(v->valuestring)>=lengths[i])valid=false;else strcpy(targets[i],v->valuestring);}}
 const char *booleans[]={"enabled","discovery","commands"};bool *flags[]={&c.enabled,&c.discovery,&c.commands};for(unsigned i=0;i<3;i++){cJSON *v=cJSON_GetObjectItemCaseSensitive(j,booleans[i]);if(v){if(!cJSON_IsBool(v))valid=false;else *flags[i]=cJSON_IsTrue(v);}}
 cJSON *port=cJSON_GetObjectItemCaseSensitive(j,"port"),*period=cJSON_GetObjectItemCaseSensitive(j,"interval_s");
 if(port){if(!cJSON_IsNumber(port)||port->valuedouble!=port->valueint||port->valueint<1||port->valueint>65535)valid=false;else c.port=port->valueint;}
 if(period){if(!cJSON_IsNumber(period)||period->valuedouble!=period->valueint||period->valueint<5||period->valueint>3600)valid=false;else c.interval=period->valueint;}
 valid=valid&&mqtt_settings_valid(c.host,c.topic,c.port,c.interval,c.enabled);cJSON_Delete(j);
 esp_err_t err=ESP_ERR_INVALID_ARG;if(valid){nvs_handle_t h;err=nvs_open("tracker_mqtt",NVS_READWRITE,&h);if(err==ESP_OK){err=nvs_set_blob(h,"config",&c,sizeof(c));if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}if(err==ESP_OK){xSemaphoreTake(lock,portMAX_DELAY);config=c;revision++;xSemaphoreGive(lock);}}
 memset(&c,0,sizeof(c));if(err!=ESP_OK)return httpd_resp_send_err(req,valid?HTTPD_500_INTERNAL_SERVER_ERROR:HTTPD_400_BAD_REQUEST,valid?"Could not save":"Invalid MQTT settings");httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
}
void tracker_mqtt_register(httpd_handle_t server){const httpd_uri_t route={.uri="/api/mqtt",.method=HTTP_POST,.handler=configure};ESP_ERROR_CHECK(httpd_register_uri_handler(server,&route));}
