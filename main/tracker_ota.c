#include "radio_settings.h"
#include "tracker_ota.h"
#include "tracker_credentials.h"
#include "tracker_backup.h"
#include "tracker_web.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_image_format.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

static const char *TAG="tracker-ota";
static char request_token[33];
static portMUX_TYPE ota_mux=portMUX_INITIALIZER_UNLOCKED;
static bool updating,pending_validation;
static size_t received_bytes,total_bytes;
static uint32_t running_image_bytes;
static int64_t restart_at;
bool tracker_ota_is_busy(void){portENTER_CRITICAL(&ota_mux);bool busy=updating||pending_validation||restart_at>0;portEXIT_CRITICAL(&ota_mux);return busy;}
bool tracker_request_authorized(httpd_req_t *req) {
    if(!tracker_backup_request_allowed(req))return false;
    char token[40];
    return request_token[0]&&httpd_req_get_hdr_value_str(req,"X-Tracker-Token",token,sizeof(token))==ESP_OK&&!strcmp(token,request_token);
}
static esp_err_t fail(httpd_req_t *req,const char *message) {
    portENTER_CRITICAL(&ota_mux);updating=false;portEXIT_CRITICAL(&ota_mux);
    return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,message);
}
static esp_err_t upload(httpd_req_t *req) {
    if(!tracker_request_authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Reload this device's page before updating");
    portENTER_CRITICAL(&ota_mux);bool blocked=updating||pending_validation||restart_at>0;portEXIT_CRITICAL(&ota_mux);
    if((tracker_credentials_copy().reset_session||tracker_radio_restarting()))blocked=true;
    if(blocked)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Update busy or firmware still being validated; retry in 30 seconds");
    const esp_partition_t *partition=esp_ota_get_next_update_partition(NULL);
    if(!partition||req->content_len<288||req->content_len>partition->size)return fail(req,"Firmware file has an invalid size");
    char type[48];
    if(httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/octet-stream"))
        return fail(req,"Upload the application .bin as application/octet-stream");
    uint8_t *buffer=malloc(2048);if(!buffer)return fail(req,"Not enough memory");
    esp_ota_handle_t handle=0;
    esp_err_t err=esp_ota_begin(partition,OTA_WITH_SEQUENTIAL_WRITES,&handle);
    if(err!=ESP_OK){free(buffer);return fail(req,"Could not open inactive firmware slot");}
    portENTER_CRITICAL(&ota_mux);updating=true;received_bytes=0;total_bytes=req->content_len;portEXIT_CRITICAL(&ota_mux);
    size_t received=0;
    while(received<(size_t)req->content_len) {
        size_t remaining=req->content_len-received;
        int n=httpd_req_recv(req,(char *)buffer,remaining<2048?remaining:2048);
        if(n<=0){esp_ota_abort(handle);free(buffer);return fail(req,"Upload interrupted; the previous firmware is unchanged");}
        err=esp_ota_write(handle,buffer,n);
        if(err!=ESP_OK){esp_ota_abort(handle);free(buffer);return fail(req,"Firmware image rejected while writing");}
        received+=n;portENTER_CRITICAL(&ota_mux);received_bytes=received;portEXIT_CRITICAL(&ota_mux);
    }
    free(buffer);err=esp_ota_end(handle);
    if(err!=ESP_OK)return fail(req,"Firmware image failed validation");
    esp_app_desc_t description;
    if(esp_ota_get_partition_description(partition,&description)!=ESP_OK||
       strcmp(description.project_name,esp_app_get_description()->project_name))
        return fail(req,"This binary belongs to a different project");
    if(esp_ota_set_boot_partition(partition)!=ESP_OK)return fail(req,"Could not select the new firmware slot");
    ESP_LOGI(TAG,"Verified OTA image in %s, version %.31s; restarting",partition->label,description.version);
    httpd_resp_set_type(req,"application/json");httpd_resp_sendstr(req,"{\"ok\":true,\"restarting\":true}");
    portENTER_CRITICAL(&ota_mux);updating=false;restart_at=esp_timer_get_time()+1500000;portEXIT_CRITICAL(&ota_mux);
    return ESP_OK;
}
void tracker_ota_register(httpd_handle_t server) {
    for(int i=0;i<4;i++)snprintf(request_token+i*8,9,"%08"PRIx32,esp_random());
    const esp_partition_t *running=esp_ota_get_running_partition();esp_ota_img_states_t state;
    if(running) {
        esp_partition_pos_t pos={.offset=running->address,.size=running->size};esp_image_metadata_t metadata;
        if(esp_image_get_metadata(&pos,&metadata)==ESP_OK)running_image_bytes=metadata.image_len;
    }
    pending_validation=esp_ota_get_state_partition(running,&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY;
    httpd_uri_t route={.uri="/api/ota",.method=HTTP_POST,.handler=upload};
    ESP_ERROR_CHECK(httpd_register_uri_handler(server,&route));
}
void tracker_ota_poll(void) {
    portENTER_CRITICAL(&ota_mux);int64_t restart=restart_at;bool pending=pending_validation;portEXIT_CRITICAL(&ota_mux);
    if(restart>0&&esp_timer_get_time()>=restart)esp_restart();
    if(pending&&esp_timer_get_time()>30000000&&esp_get_free_heap_size()>16000) {
        if(esp_ota_mark_app_valid_cancel_rollback()==ESP_OK) {
            portENTER_CRITICAL(&ota_mux);pending_validation=false;portEXIT_CRITICAL(&ota_mux);
            ESP_LOGI(TAG,"OTA boot accepted after stable main-loop operation");
        }
    }
}
void tracker_ota_json(cJSON *root) {
    cJSON *ota=cJSON_AddObjectToObject(root,"firmware");if(!ota)return;
    const esp_app_desc_t *app=esp_app_get_description();
    const esp_partition_t *current=esp_ota_get_running_partition(),*next=esp_ota_get_next_update_partition(NULL);
    cJSON_AddStringToObject(ota,"version",app->version);cJSON_AddStringToObject(ota,"running_slot",current?current->label:"");
    cJSON_AddStringToObject(ota,"next_slot",next?next->label:"");
    cJSON_AddNumberToObject(ota,"slot_bytes",next?next->size:0);
    cJSON_AddNumberToObject(ota,"image_bytes",running_image_bytes);
    cJSON_AddNumberToObject(ota,"flash_bytes",8*1048576);
    cJSON_AddNumberToObject(ota,"free_heap_bytes",esp_get_free_heap_size());
    cJSON_AddNumberToObject(ota,"min_free_heap_bytes",esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(ota,"largest_free_block",heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    portENTER_CRITICAL(&ota_mux);bool busy=updating,pending=pending_validation;size_t received=received_bytes,total=total_bytes;portEXIT_CRITICAL(&ota_mux);
    cJSON_AddBoolToObject(ota,"updating",busy);cJSON_AddBoolToObject(ota,"pending_validation",pending);
    cJSON_AddNumberToObject(ota,"received_bytes",received);cJSON_AddNumberToObject(ota,"total_bytes",total);
    cJSON_AddStringToObject(root,"request_token",request_token);
}
