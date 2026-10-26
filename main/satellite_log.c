#include "satellite_log.h"
#include "sat_ring.h"
#include "sat_import.h"
#include "tracker_web.h"
#include "tracker_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "nvs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#define ADDRESS 0x420000
#define BYTES 0x80000
#define BLOCKS (BYTES/SAT_SECTOR)
static sat_ring_t ring;
static sat_block_t blocks[BLOCKS];
static sat_order_t order[BLOCKS];
static uint8_t scratch[SAT_SECTOR];
static SemaphoreHandle_t mutex;
static uint32_t interval=10,boot;
static bool enabled=true,segment=true;
static int64_t next_sample;
static const char *state="starting";
static bool rd(void *p,size_t o,void *b,size_t n){return esp_partition_read(p,o,b,n)==ESP_OK;}
static bool wr(void *p,size_t o,const void *b,size_t n){return esp_partition_write(p,o,b,n)==ESP_OK;}
static bool er(void *p,size_t o,size_t n){return esp_partition_erase_range(p,o,n)==ESP_OK;}
bool satellite_log_init(void){
 mutex=xSemaphoreCreateMutex();boot=esp_random();if(!boot)boot=1;if(!mutex)return false;
 const esp_partition_t *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x41,"sat_history");
 if(!part&&esp_partition_register_external(NULL,ADDRESS,BYTES,"sat_history",ESP_PARTITION_TYPE_DATA,0x41,&part)!=ESP_OK){state="storage_error";return false;}
 if(!part||part->address!=ADDRESS||part->size!=BYTES){state="storage_error";return false;}
 uint64_t floor=1;nvs_handle_t h;if(nvs_open("sat_history",NVS_READONLY,&h)==ESP_OK){uint8_t on=1;nvs_get_u8(h,"enabled",&on);enabled=on!=0;nvs_get_u32(h,"interval",&interval);nvs_get_u64(h,"floor",&floor);nvs_close(h);}if(interval<5||interval>3600)interval=10;
 bool ok=sat_ring_mount(&ring,(track_io_t){.context=(void *)part,.read=rd,.write=wr,.erase=er},blocks,BLOCKS,scratch,floor);if(ok)ok=sat_ring_index(&ring,order);state=ok?(enabled?"recording":"disabled"):"storage_error";return ok;
}
void satellite_log_poll(const tracker_gnss_t *g,int64_t now){
 if(!mutex||xSemaphoreTake(mutex,0)!=pdTRUE)return;
 if(!ring.ready){state="storage_error";goto done;}if(!enabled){state="disabled";segment=true;goto done;}state="recording";if(now<next_sample)goto done;
 sat_record_t p={.uptime_s=now/1000,.boot=boot,.total=255,.flags=segment?1:0};memset(p.visible,255,7);memset(p.used,255,7);
 if(g->utc_received_ms>=0&&now>=g->utc_received_ms&&now-g->utc_received_ms<10000){p.utc_s=(g->utc_epoch_ms+now-g->utc_received_ms)/1000;if(g->utc_confirmed)p.flags|=4;}
 if(tracker_gnss_has_fix(g,now))p.flags|=2;
 if(g->gga_ms>=0&&now-g->gga_ms<10000)p.total=g->used;
 static tracker_sat_snapshot_t snap;tracker_sat_snapshot(g,now,0,&snap);
 for(unsigned i=0;i<7;i++){if(g->gsv_system_ms[i+1]>=0&&now-g->gsv_system_ms[i+1]<15000)p.visible[i]=0;if(snap.usage_known_mask&(1<<i))p.used[i]=0;}
 for(unsigned i=0;i<snap.count;i++){tracker_sat_view_t *s=&snap.satellites[i];if(s->system<1||s->system>7)continue;unsigned n=s->system-1;if((s->flags&SAT_VISIBLE)&&p.visible[n]!=255)p.visible[n]++;if((s->flags&SAT_USED)&&p.used[n]!=255)p.used[n]++;}
 if(p.utc_s?sat_ring_merge(&ring,&p)>=0:sat_ring_append(&ring,&p)){segment=false;next_sample=now+(int64_t)interval*1000;}else state="storage_error";
 done:xSemaphoreGive(mutex);
}
void satellite_log_json(cJSON *root){
 cJSON *j=cJSON_AddObjectToObject(root,"sat_history");if(!j)return;if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(50))!=pdTRUE){cJSON_AddStringToObject(j,"state","busy");return;}
 cJSON_AddStringToObject(j,"state",state);cJSON_AddBoolToObject(j,"ready",ring.ready);cJSON_AddBoolToObject(j,"enabled",enabled);cJSON_AddNumberToObject(j,"interval_s",interval);cJSON_AddNumberToObject(j,"count",ring.count);cJSON_AddNumberToObject(j,"capacity",BLOCKS*SAT_SLOTS);cJSON_AddNumberToObject(j,"bytes",BYTES);cJSON_AddNumberToObject(j,"epoch",(double)ring.floor);cJSON_AddNumberToObject(j,"next_seq",(double)ring.next_seq);cJSON_AddNumberToObject(j,"oldest_seq",(double)sat_ring_oldest(&ring));cJSON_AddNumberToObject(j,"boot",boot);cJSON_AddBoolToObject(j,"merge_supported",true);cJSON_AddBoolToObject(j,"recent_paging",true);cJSON_AddNumberToObject(j,"retention_revision",(double)(ring.next_seq-ring.count));
 sat_record_t point;uint64_t oldest=sat_ring_oldest(&ring);
 if(ring.count&&sat_ring_read(&ring,oldest-1,&point,1)){
  int64_t utc=(int64_t)point.utc_s*1000;bool estimated=false;
  /* A pre-UTC sample can use a later receiver reference from the same boot.
     Bounded lookup; an unknown time stays unknown, never uses a browser clock. */
  if(!utc){sat_record_t first=point;static sat_record_t anchors[16];uint64_t cursor=first.seq;
   for(unsigned page=0;page<8&&!utc;page++){size_t n=sat_ring_read(&ring,cursor,anchors,16);if(!n)break;bool other_boot=false;
    for(size_t i=0;i<n;i++){sat_record_t *a=&anchors[i];cursor=a->seq;if(a->boot!=first.boot){other_boot=true;break;}if(a->utc_s&&(a->flags&4)){utc=(int64_t)a->utc_s*1000+((int64_t)first.uptime_s-a->uptime_s)*1000;estimated=true;break;}}
    if(other_boot)break;
   }
  }
  if(utc){cJSON_AddNumberToObject(j,"oldest_utc_ms",(double)utc);cJSON_AddBoolToObject(j,"oldest_time_estimated",estimated);}
 }
 uint32_t min_utc=UINT32_MAX,max_utc=0;for(unsigned i=0;i<BLOCKS;i++)if(blocks[i].count){if(order[i].min_utc&&order[i].min_utc<min_utc)min_utc=order[i].min_utc;if(order[i].max_utc>max_utc)max_utc=order[i].max_utc;}
 cJSON *oldest_field=cJSON_GetObjectItemCaseSensitive(j,"oldest_utc_ms");
 if(min_utc!=UINT32_MAX&&(!oldest_field||min_utc*1000.0<oldest_field->valuedouble)){if(oldest_field)cJSON_SetNumberValue(oldest_field,min_utc*1000.0);else cJSON_AddNumberToObject(j,"oldest_utc_ms",min_utc*1000.0);cJSON_DeleteItemFromObject(j,"oldest_time_estimated");}
 if(max_utc)cJSON_AddNumberToObject(j,"newest_utc_ms",max_utc*1000.0);
 uint32_t available=0;bool free_block=false;if(ring.ready){sat_block_t *cur=&blocks[ring.current];if(cur->valid)available=SAT_SLOTS-cur->used;for(unsigned i=0;i<BLOCKS;i++)if(i!=ring.current&&!blocks[i].count){available+=SAT_SLOTS;free_block=true;}}
 if(available>BLOCKS*SAT_SLOTS-ring.count)available=BLOCKS*SAT_SLOTS-ring.count;
 cJSON_AddNumberToObject(j,"slots_before_overwrite",available);cJSON_AddBoolToObject(j,"overwriting",!free_block&&ring.next_seq-ring.floor>ring.count);xSemaphoreGive(mutex);
}
static bool arg(const char *q,const char *key,uint64_t *n){char b[24];esp_err_t e=httpd_query_key_value(q,key,b,sizeof(b));if(e==ESP_ERR_NOT_FOUND)return true;if(e!=ESP_OK||!b[0]||b[0]=='-')return false;char *end;unsigned long long value=strtoull(b,&end,10);if(*end||value>UINT32_MAX)return false;*n=value;return true;}
static esp_err_t get(httpd_req_t *req){
 char q[180]={0},ordering[16]={0};uint64_t after=0,limit=128,before_utc=UINT32_MAX,before_seq=UINT32_MAX,since=0;if(httpd_req_get_url_query_len(req)>=sizeof(q))return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Query too long");httpd_req_get_url_query_str(req,q,sizeof(q));esp_err_t order_result=httpd_query_key_value(q,"order",ordering,sizeof(ordering));bool recent=order_result==ESP_OK&&!strcmp(ordering,"recent");if((order_result!=ESP_ERR_NOT_FOUND&&!recent)||!arg(q,"after",&after)||!arg(q,"limit",&limit)||!arg(q,"before_s",&before_utc)||!arg(q,"before_seq",&before_seq)||!arg(q,"since_s",&since)||limit<1||limit>128)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid cursor/limit");
 sat_record_t *points=malloc((limit+(recent?1:0))*sizeof(*points));if(!points)return ESP_FAIL;
 if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))!=pdTRUE){free(points);return ESP_FAIL;}if(!ring.ready){xSemaphoreGive(mutex);free(points);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage unavailable");}
 size_t count=recent?sat_ring_read_recent(&ring,before_utc,before_seq,since,points,limit+1):sat_ring_read(&ring,after,points,limit);bool more=recent&&count>limit;if(count>limit)count=limit;bool ready=ring.ready;uint64_t floor=ring.floor,next=ring.next_seq,oldest=sat_ring_oldest(&ring);uint32_t total=ring.count;uint64_t revision=next-total;xSemaphoreGive(mutex);if(!ready){free(points);return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage unavailable");}
 httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");char buffer[1024];int n=snprintf(buffer,sizeof(buffer),"{\"epoch\":%"PRIu64",\"next_seq\":%"PRIu64",\"oldest_seq\":%"PRIu64",\"total\":%"PRIu32",\"retention_revision\":%"PRIu64",\"recent\":%s,\"more\":%s,\"points\":[",floor,next,oldest,total,revision,recent?"true":"false",more?"true":"false");esp_err_t err=httpd_resp_send_chunk(req,buffer,n);size_t buffered=0;
 for(size_t i=0;i<count&&err==ESP_OK;i++){sat_record_t *p=&points[i];cJSON *j=cJSON_CreateObject();cJSON_AddNumberToObject(j,"seq",p->seq);if(p->utc_s)cJSON_AddNumberToObject(j,"t",(double)p->utc_s*1000);else cJSON_AddNullToObject(j,"t");cJSON_AddNumberToObject(j,"boot",p->boot);cJSON_AddNumberToObject(j,"up",p->uptime_s);cJSON_AddBoolToObject(j,"break",p->flags&1);cJSON_AddBoolToObject(j,"fix",p->flags&2);cJSON_AddBoolToObject(j,"time_confirmed",p->flags&4);cJSON_AddBoolToObject(j,"imported",p->flags&SAT_IMPORTED);cJSON_AddBoolToObject(j,"time_estimated",p->flags&SAT_TIME_DERIVED);if(p->total==255)cJSON_AddNullToObject(j,"total");else cJSON_AddNumberToObject(j,"total",p->total);cJSON *v=cJSON_AddArrayToObject(j,"v"),*u=cJSON_AddArrayToObject(j,"u");for(int k=0;k<7;k++){cJSON_AddItemToArray(v,p->visible[k]==255?cJSON_CreateNull():cJSON_CreateNumber(p->visible[k]));cJSON_AddItemToArray(u,p->used[k]==255?cJSON_CreateNull():cJSON_CreateNumber(p->used[k]));}char *body=cJSON_PrintUnformatted(j);cJSON_Delete(j);if(!body){err=ESP_FAIL;break;}size_t length=strlen(body),comma=i?1:0;if(buffered+comma+length>sizeof(buffer)){err=httpd_resp_send_chunk(req,buffer,buffered);buffered=0;}if(err==ESP_OK){if(comma)buffer[buffered++]=',';if(length>sizeof(buffer)){if(buffered)err=httpd_resp_send_chunk(req,buffer,buffered);buffered=0;if(err==ESP_OK)err=httpd_resp_sendstr_chunk(req,body);}else{memcpy(buffer+buffered,body,length);buffered+=length;}}free(body);}
 free(points);if(err==ESP_OK&&buffered)err=httpd_resp_send_chunk(req,buffer,buffered);if(err==ESP_OK)err=httpd_resp_send_chunk(req,"]}",2);if(err==ESP_OK)err=httpd_resp_send_chunk(req,NULL,0);return err;
}
static esp_err_t post(httpd_req_t *req){
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Reload page first");
 if(req->content_len<2||req->content_len>180)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid size");
 char body[181];int n=0;while(n<req->content_len){int got=httpd_req_recv(req,body+n,req->content_len-n);if(got<=0)return ESP_FAIL;n+=got;}body[n]=0;cJSON *j=cJSON_Parse(body),*a=cJSON_GetObjectItemCaseSensitive(j,"action"),*on=cJSON_GetObjectItemCaseSensitive(j,"enabled"),*period=cJSON_GetObjectItemCaseSensitive(j,"interval_s");bool clear=cJSON_IsString(a)&&!strcmp(a->valuestring,"clear");bool valid=clear||(cJSON_IsString(a)&&!strcmp(a->valuestring,"configure")&&(!on||cJSON_IsBool(on))&&(!period||(cJSON_IsNumber(period)&&period->valuedouble==period->valueint&&period->valueint>=5&&period->valueint<=3600)));if(!valid){cJSON_Delete(j);return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid settings");}
 if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(1000))!=pdTRUE){cJSON_Delete(j);return ESP_FAIL;}nvs_handle_t h;esp_err_t err=ring.ready?nvs_open("sat_history",NVS_READWRITE,&h):ESP_FAIL;if(err==ESP_OK){if(clear)err=nvs_set_u64(h,"floor",ring.next_seq);else{if(on)err=nvs_set_u8(h,"enabled",cJSON_IsTrue(on));if(err==ESP_OK&&period)err=nvs_set_u32(h,"interval",period->valueint);}if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);}if(err==ESP_OK){if(clear)sat_ring_clear(&ring);else{if(on)enabled=cJSON_IsTrue(on);if(period)interval=period->valueint;}segment=true;next_sample=0;}xSemaphoreGive(mutex);cJSON_Delete(j);if(err!=ESP_OK)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Could not save settings");httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");
}
static esp_err_t import_history(httpd_req_t *req){
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Request token required");
 if(req->content_len<2||req->content_len>8192)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid size");
 char *body=malloc(req->content_len+1);if(!body)return ESP_FAIL;int got=0;
 while(got<req->content_len){int n=httpd_req_recv(req,body+got,req->content_len-got);if(n<=0){free(body);return ESP_FAIL;}got+=n;}body[got]=0;cJSON *j=cJSON_Parse(body);free(body);
 cJSON *list=cJSON_GetObjectItemCaseSensitive(j,"points"),*offset=cJSON_GetObjectItemCaseSensitive(j,"offset");int count=cJSON_GetArraySize(list);
 bool valid=cJSON_IsArray(list)&&count>=1&&count<=16&&cJSON_IsNumber(offset)&&offset->valuedouble==offset->valueint&&offset->valueint>=0&&offset->valueint<=1000000;sat_record_t points[16];uint32_t previous=0;
 for(int i=0;valid&&i<count;i++){valid=sat_import_parse(cJSON_GetArrayItem(list,i),&points[i]);if(valid){valid=points[i].utc_s>=previous;previous=points[i].utc_s;}}
 int next=valid?offset->valueint+count:0;cJSON_Delete(j);if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Confirmed GNSS-timed ordered statistics required");
 if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(2000))!=pdTRUE)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Storage busy");
 unsigned added=0,duplicates=0,too_old=0;bool ok=true;
 for(int i=0;i<count;i++){int result=sat_ring_merge(&ring,&points[i]);if(result==1)added++;else if(result==0)duplicates++;else if(result==-2)too_old++;else{ok=false;break;}}
 xSemaphoreGive(mutex);if(!ok)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Merge interrupted; retries skip identical samples");
 char reply[160];snprintf(reply,sizeof(reply),"{\"ok\":true,\"next_offset\":%d,\"added\":%u,\"duplicates\":%u,\"too_old\":%u}",next,added,duplicates,too_old);httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");return httpd_resp_sendstr(req,reply);
}
void satellite_log_register(httpd_handle_t server){const httpd_uri_t routes[]={{.uri="/api/history",.method=HTTP_GET,.handler=get},{.uri="/api/history",.method=HTTP_POST,.handler=post},{.uri="/api/history/import",.method=HTTP_POST,.handler=import_history}};for(unsigned i=0;i<3;i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));}
