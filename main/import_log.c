/* Old import area is read only for migration; all new points enter the common track. */
#include "import_log.h"
#include "tracker_log.h"
#include "tracker_ota.h"
#include "tracker_web.h"
#include "esp_partition.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#define BLOCKS 96
typedef struct {track_ring_t legacy;track_block_t blocks[BLOCKS];uint8_t scratch[TRACK_SECTOR];} migration_workspace_t;
static bool migrated;
static bool rd(void *p,size_t o,void *b,size_t n){return esp_partition_read(p,o,b,n)==ESP_OK;}
static bool wr(void *p,size_t o,const void *b,size_t n){return esp_partition_write(p,o,b,n)==ESP_OK;}
static bool er(void *p,size_t o,size_t n){return esp_partition_erase_range(p,o,n)==ESP_OK;}
bool import_log_init(void){
 const esp_partition_t *part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x42,"track_import");
 if(!part&&esp_partition_register_external(NULL,0x4a0000,0x60000,"track_import",ESP_PARTITION_TYPE_DATA,0x42,&part)!=ESP_OK)return false;
 if(!part||part->address!=0x4a0000||part->size!=0x60000)return false;
 migration_workspace_t *work=calloc(1,sizeof(*work));if(!work)return false;
 track_ring_t *legacy=&work->legacy;bool ok=false;
 uint64_t floor=1;nvs_handle_t h;if(nvs_open("track_import",NVS_READONLY,&h)==ESP_OK){nvs_get_u64(h,"floor",&floor);nvs_close(h);}
 if(!track_ring_mount(legacy,(track_io_t){.context=(void *)part,.read=rd,.write=wr,.erase=er},work->blocks,BLOCKS,work->scratch,floor))goto done;
 track_record_t points[16];uint64_t cursor=0;uint32_t copied=0;
 while(copied<legacy->count){size_t count=track_ring_read(legacy,cursor,points,16);if(!count)goto done;cursor=points[count-1].seq;unsigned added,duplicates;if(!tracker_log_merge_records(points,count,&added,&duplicates))goto done;copied+=count;}
 if(legacy->count){if(nvs_open("track_import",NVS_READWRITE,&h)!=ESP_OK)goto done;esp_err_t err=nvs_set_u64(h,"floor",legacy->next_seq);if(err==ESP_OK)err=nvs_commit(h);nvs_close(h);if(err!=ESP_OK)goto done;}
 migrated=ok=true;
 done:free(work);return ok;
}
void import_log_json(cJSON *root){cJSON_AddBoolToObject(root,"track_merge_ready",migrated);}
static bool number(cJSON *v,double low,double high){return cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=low&&v->valuedouble<=high;}
static esp_err_t merge(httpd_req_t *req){
 if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Request token required");
 if(!migrated)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Legacy track migration incomplete");
 if(req->content_len<2||req->content_len>8192)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid size");
 char *body=malloc(req->content_len+1);if(!body)return ESP_FAIL;int got=0;while(got<req->content_len){int n=httpd_req_recv(req,body+got,req->content_len-got);if(n<=0){free(body);return ESP_FAIL;}got+=n;}body[got]=0;cJSON *j=cJSON_Parse(body);free(body);cJSON *list=cJSON_GetObjectItemCaseSensitive(j,"points"),*offset=cJSON_GetObjectItemCaseSensitive(j,"offset");int count=cJSON_GetArraySize(list);bool valid=cJSON_IsArray(list)&&count>0&&count<=32&&number(offset,0,1000000)&&offset->valuedouble==offset->valueint;track_record_t points[32]={0};int64_t previous=0;
 for(int i=0;valid&&i<count;i++){cJSON *p=cJSON_GetArrayItem(list,i),*t=cJSON_GetObjectItemCaseSensitive(p,"t"),*lat=cJSON_GetObjectItemCaseSensitive(p,"lat"),*lon=cJSON_GetObjectItemCaseSensitive(p,"lon"),*alt=cJSON_GetObjectItemCaseSensitive(p,"alt"),*source=cJSON_GetObjectItemCaseSensitive(p,"time_source"),*br=cJSON_GetObjectItemCaseSensitive(p,"break");valid=number(t,315532800000LL,4102444799999LL)&&t->valuedouble==floor(t->valuedouble)&&t->valuedouble>=previous&&number(lat,-90,90)&&number(lon,-180,180)&&cJSON_IsString(source)&&!strcmp(source->valuestring,"gnss");if(!valid)break;previous=t->valuedouble;points[i]=(track_record_t){.utc_ms=previous,.lat_e7=llround(lat->valuedouble*1e7),.lon_e7=llround(lon->valuedouble*1e7),.altitude=number(alt,-32767,32767)?lround(alt->valuedouble):INT16_MIN,.flags=TRACK_IMPORTED|(cJSON_IsTrue(br)?TRACK_BREAK:0)};}
 unsigned added=0,duplicates=0;bool ok=valid&&tracker_log_merge_records(points,count,&added,&duplicates);int next=valid?offset->valueint+count:0;cJSON_Delete(j);
 if(!valid)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"GNSS-timed ordered positions required; legacy browser timestamps are not converted");
 if(!ok)return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"Merge interrupted; retry safely without duplicate points");
 char reply[128];snprintf(reply,sizeof(reply),"{\"ok\":true,\"next_offset\":%d,\"added\":%u,\"duplicates\":%u}",next,added,duplicates);httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,reply);
}
void import_log_register(httpd_handle_t server){const httpd_uri_t route={.uri="/api/import",.method=HTTP_POST,.handler=merge};ESP_ERROR_CHECK(httpd_register_uri_handler(server,&route));}
