#include "tracker_log.h"
#include "track_ring.h"
#include "tracker_ota.h"
#include "tracker_backup.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <inttypes.h>
#define LOG_ADDRESS 0x500000
#define LOG_BYTES 0x300000
#define BLOCKS (LOG_BYTES/TRACK_SECTOR)
static const char *TAG="device-track";
static const esp_partition_t *partition;
static track_ring_t ring;
static track_block_t blocks[BLOCKS];
static track_order_t order[BLOCKS];
static uint8_t scratch[TRACK_SECTOR];
static SemaphoreHandle_t mutex;
static bool enabled=true,segment=true;
static uint32_t interval=10;
static int64_t next_sample;
static const char *state="starting";
static bool read_flash(void *ctx,size_t offset,void *out,size_t n){return esp_partition_read(ctx,offset,out,n)==ESP_OK;}
static bool write_flash(void *ctx,size_t offset,const void *data,size_t n){return esp_partition_write(ctx,offset,data,n)==ESP_OK;}
static bool erase_flash(void *ctx,size_t offset,size_t n){return esp_partition_erase_range(ctx,offset,n)==ESP_OK;}
bool tracker_log_init(void){
 mutex=xSemaphoreCreateMutex();if(!mutex)return false;
 partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,(esp_partition_subtype_t)0x40,"tracks");
 if(!partition&&esp_partition_register_external(NULL,LOG_ADDRESS,LOG_BYTES,"tracks",ESP_PARTITION_TYPE_DATA,(esp_partition_subtype_t)0x40,&partition)!=ESP_OK){state="storage_error";return false;}
 if(!partition||partition->address!=LOG_ADDRESS||partition->size!=LOG_BYTES){state="storage_error";return false;}
 uint64_t floor=1;nvs_handle_t h;if(nvs_open("tracker_log",NVS_READONLY,&h)==ESP_OK){uint8_t on=1;nvs_get_u8(h,"enabled",&on);enabled=on!=0;nvs_get_u32(h,"interval",&interval);nvs_get_u64(h,"floor",&floor);nvs_close(h);}if(interval<5||interval>3600)interval=10;
 bool ok=track_ring_mount(&ring,(track_io_t){.context=(void *)partition,.read=read_flash,.write=write_flash,.erase=erase_flash},blocks,BLOCKS,scratch,floor);
 if(ok)ok=track_ring_index(&ring,order);
 state=ok?(enabled?"waiting_fix":"disabled"):"storage_error";ESP_LOGI(TAG,"Track storage ready=%d, records=%"PRIu32", 3 MiB outside OTA/NVS",ok,ring.count);return ok;
}
void tracker_log_poll(const tracker_gnss_t *g,int64_t now){
 if(!mutex||xSemaphoreTake(mutex,0)!=pdTRUE)return;
 if(!ring.ready){state="storage_error";goto done;}
 if(!enabled){state="disabled";segment=true;goto done;}
 if(!tracker_gnss_has_fix(g,now)){state="waiting_fix";segment=true;goto done;}
 int64_t measurement=tracker_gnss_position_utc(g,now);
 if(measurement<0){state="waiting_time";segment=true;goto done;}
 state="recording";if(now<next_sample)goto done;
 track_record_t p={.utc_ms=measurement,.lat_e7=(int32_t)llround(g->latitude*1e7),.lon_e7=(int32_t)llround(g->longitude*1e7),.altitude=isfinite(g->altitude)?(int16_t)fmax(-32767,fmin(32767,round(g->altitude))):INT16_MIN,.used=g->used,.flags=segment?TRACK_BREAK:0};
 if(track_ring_merge(&ring,&p)>=0){segment=false;next_sample=now+(int64_t)interval*1000;}else state="storage_error";
 done:xSemaphoreGive(mutex);
}
bool tracker_log_configure(int on,int period){
 if(tracker_backup_locked())return false;
 if(period&& (period<5||period>3600))return false;
 if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))!=pdTRUE)return false;
 if(on<0)on=enabled;
 if(enabled==on&&(!period||interval==period)){xSemaphoreGive(mutex);return true;}
 nvs_handle_t h;esp_err_t err=nvs_open("tracker_log",NVS_READWRITE,&h);if(err==ESP_OK){err=nvs_set_u8(h,"enabled",on);if(err==ESP_OK&&period)err=nvs_set_u32(h,"interval",period);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}
 if(err==ESP_OK){enabled=on;if(period)interval=period;segment=true;next_sample=0;}
 xSemaphoreGive(mutex);return err==ESP_OK;
}
void tracker_log_json(cJSON *root){
 cJSON *log=cJSON_AddObjectToObject(root,"track");if(!log)return;
 if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE){cJSON_AddStringToObject(log,"state","busy");return;}
 cJSON_AddStringToObject(log,"state",state);cJSON_AddBoolToObject(log,"enabled",enabled);cJSON_AddBoolToObject(log,"ready",ring.ready);cJSON_AddNumberToObject(log,"interval_s",interval);cJSON_AddNumberToObject(log,"count",ring.count);cJSON_AddNumberToObject(log,"capacity",BLOCKS*TRACK_SLOTS);cJSON_AddNumberToObject(log,"bytes",LOG_BYTES);cJSON_AddNumberToObject(log,"used_bytes",ring.count*32);cJSON_AddNumberToObject(log,"next_seq",(double)ring.next_seq);cJSON_AddNumberToObject(log,"oldest_seq",(double)track_ring_oldest(&ring));cJSON_AddNumberToObject(log,"epoch",(double)ring.floor);
 int64_t oldest_time=INT64_MAX,newest_time=0;for(unsigned i=0;i<BLOCKS;i++)if(blocks[i].count){if(order[i].min_utc<oldest_time)oldest_time=order[i].min_utc;if(order[i].max_utc>newest_time)newest_time=order[i].max_utc;}
 if(ring.count){cJSON_AddNumberToObject(log,"oldest_utc_ms",(double)oldest_time);cJSON_AddNumberToObject(log,"newest_utc_ms",(double)newest_time);}
 cJSON_AddBoolToObject(log,"unified",true);cJSON_AddNumberToObject(log,"retention_revision",(double)(ring.next_seq-ring.count));
 /* Available physical slots until an existing visible sector must be retired. */
 uint32_t available=0;bool free_block=false;if(ring.ready){track_block_t *cur=&blocks[ring.current];if(cur->valid)available=TRACK_SLOTS-cur->used;for(unsigned i=0;i<BLOCKS;i++)if(i!=ring.current&&!blocks[i].count){available+=TRACK_SLOTS;free_block=true;}}
 if(available>BLOCKS*TRACK_SLOTS-ring.count)available=BLOCKS*TRACK_SLOTS-ring.count;
 cJSON_AddNumberToObject(log,"slots_before_overwrite",available);cJSON_AddBoolToObject(log,"overwriting",!free_block&&ring.next_seq-ring.floor>ring.count);xSemaphoreGive(mutex);
}
static bool query_number(const char *query,const char *key,uint64_t *out){char value[24];esp_err_t err=httpd_query_key_value(query,key,value,sizeof(value));if(err==ESP_ERR_NOT_FOUND)return true;if(err!=ESP_OK||!value[0]||value[0]=='-')return false;char *end;unsigned long long n=strtoull(value,&end,10);if(*end||n>9007199254740991ULL)return false;*out=n;return true;}
static esp_err_t get_track(httpd_req_t *req){
 char query[160]={0};uint64_t after=0,limit=512,after_time=0,time_seq=0;if(httpd_req_get_url_query_len(req)>=sizeof(query))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Query too long");httpd_req_get_url_query_str(req,query,sizeof(query));
 if(!query_number(query,"after",&after)||!query_number(query,"limit",&limit)||!query_number(query,"after_time",&after_time)||!query_number(query,"time_seq",&time_seq)||!limit||limit>512)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid after/limit");
 track_record_t *points=malloc(limit*sizeof(*points));if(!points)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"No memory");
 if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))!=pdTRUE){free(points);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage busy");}
 if(!ring.ready){xSemaphoreGive(mutex);free(points);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage unavailable");}
 char sorted[4]={0};bool chronological=httpd_query_key_value(query,"ordered",sorted,sizeof(sorted))==ESP_OK&&!strcmp(sorted,"1");
 size_t n=chronological?track_ring_read_time(&ring,after_time,time_seq,points,limit):track_ring_read(&ring,after,points,limit);uint64_t floor=ring.floor,next=ring.next_seq,oldest=track_ring_oldest(&ring);uint32_t total=ring.count;xSemaphoreGive(mutex);
 httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");char buffer[2048];size_t used=snprintf(buffer,sizeof(buffer),"{\"epoch\":%"PRIu64",\"oldest_seq\":%"PRIu64",\"next_seq\":%"PRIu64",\"total\":%"PRIu32",\"points\":[",floor,oldest,next,total);esp_err_t err=ESP_OK;
 for(size_t i=0;i<n;i++){
  track_record_t *p=&points[i];char altitude[16];if(p->altitude==INT16_MIN)strcpy(altitude,"null");else snprintf(altitude,sizeof(altitude),"%d",p->altitude);
  if(used>sizeof(buffer)-240){err=httpd_resp_send_chunk(req,buffer,used);used=0;if(err!=ESP_OK)break;}
  used+=snprintf(buffer+used,sizeof(buffer)-used,"%s{\"seq\":%"PRIu64",\"t\":%"PRId64",\"lat\":%.7f,\"lon\":%.7f,\"alt\":%s,\"used\":%u,\"imported\":%s,\"time_source\":\"%s\",\"break\":%s}",i?",":"",p->seq,p->utc_ms,p->lat_e7/1e7,p->lon_e7/1e7,altitude,p->used,(p->flags&TRACK_IMPORTED)?"true":"false",(p->flags&TRACK_BROWSER_TIME)?"browser_legacy":"gnss",(p->flags&TRACK_BREAK)?"true":"false");
 }
 free(points);if(err!=ESP_OK)return err;used+=snprintf(buffer+used,sizeof(buffer)-used,"]}");err=httpd_resp_send_chunk(req,buffer,used);if(err==ESP_OK)err=httpd_resp_send_chunk(req,NULL,0);return err;
}
static esp_err_t configure_track(httpd_req_t *req){
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Reload this page first");
 if(req->content_len<2||req->content_len>160){return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid request");}
 char body[161];int got=0;while(got<req->content_len){int n=httpd_req_recv(req,body+got,req->content_len-got);if(n<=0)return ESP_FAIL;got+=n;}body[got]=0;cJSON *json=cJSON_Parse(body);cJSON *action=cJSON_GetObjectItemCaseSensitive(json,"action"),*on=cJSON_GetObjectItemCaseSensitive(json,"enabled"),*period=cJSON_GetObjectItemCaseSensitive(json,"interval_s");
 bool clear=cJSON_IsString(action)&&!strcmp(action->valuestring,"clear");bool valid=clear||(cJSON_IsString(action)&&!strcmp(action->valuestring,"configure")&&(!on||cJSON_IsBool(on))&&(!period||(cJSON_IsNumber(period)&&period->valuedouble==period->valueint&&period->valueint>=5&&period->valueint<=3600)));
 if(!valid){cJSON_Delete(json);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid settings");}
 if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))!=pdTRUE){cJSON_Delete(json);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage busy");}
 if(clear&&!ring.ready){xSemaphoreGive(mutex);cJSON_Delete(json);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Track storage unavailable");}
 nvs_handle_t h;esp_err_t err=nvs_open("tracker_log",NVS_READWRITE,&h);if(err==ESP_OK){if(clear)err=nvs_set_u64(h,"floor",ring.next_seq);else{if(on)err=nvs_set_u8(h,"enabled",cJSON_IsTrue(on));if(err==ESP_OK&&period)err=nvs_set_u32(h,"interval",period->valueint);}if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}
 if(err==ESP_OK){if(clear)track_ring_clear(&ring);else{if(on)enabled=cJSON_IsTrue(on);if(period)interval=period->valueint;}segment=true;next_sample=0;}
 xSemaphoreGive(mutex);cJSON_Delete(json);if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save settings");httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
}
void tracker_log_register(httpd_handle_t server){const httpd_uri_t routes[]={{.uri="/api/track",.method=HTTP_GET,.handler=get_track},{.uri="/api/track",.method=HTTP_POST,.handler=configure_track}};for(unsigned i=0;i<2;i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));}

bool tracker_log_merge_records(track_record_t *points,size_t count,unsigned *added,unsigned *duplicates){
 *added=*duplicates=0;if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(2000))!=pdTRUE)return false;
 bool ok=ring.ready;for(size_t i=0;ok&&i<count;i++){points[i].flags|=TRACK_IMPORTED;int result=track_ring_merge(&ring,&points[i]);if(result<0)ok=false;else if(result)(*added)++;else (*duplicates)++;}
 xSemaphoreGive(mutex);return ok;
}
