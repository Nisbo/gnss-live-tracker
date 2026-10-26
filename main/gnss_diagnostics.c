#include "gnss_diagnostics.h"
#include "tracker_web.h"
#include "tracker_ota.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#define DIAG_LINES 20
static struct {char text[256];int64_t at;} lines[DIAG_LINES];
static SemaphoreHandle_t mutex;
static bool probe;
void gnss_diagnostics_init(void){mutex=xSemaphoreCreateMutex();}
bool gnss_diagnostics_take_probe(void){if(!mutex||xSemaphoreTake(mutex,0)!=pdTRUE)return false;bool result=probe;probe=false;xSemaphoreGive(mutex);return result;}
void gnss_diagnostics_line(const char *line,int64_t now){
 int slot=-1;if(!strncmp(line,"$CFGSYS,",8))slot=0;else if(!strncmp(line,"$CFGNAV,",8))slot=1;else if(!strncmp(line,"$CFGMSG,",8))slot=2;
 else if(strlen(line)>6){if(!strncmp(line+3,"GGA",3))slot=3;else if(!strncmp(line+3,"GST",3))slot=4;else if(!strncmp(line+3,"GSA",3)){const char *last=strrchr(line,',');int source=last?atoi(last+1):0;if(source<0||source>6)source=0;slot=5+source;}else if(!strncmp(line+3,"GSV",3)){const char *talkers[]={"GP","GL","GA","GB","BD","GQ","GI","GN"};for(int i=0;i<8;i++)if(!strncmp(line+1,talkers[i],2))slot=12+i;}}
 if(slot<0||strlen(line)>=256||!mutex||xSemaphoreTake(mutex,0)!=pdTRUE)return;
 strcpy(lines[slot].text,line);lines[slot].at=now;xSemaphoreGive(mutex);
}
static bool authorized(httpd_req_t *req){return tracker_request_authorized(req);}
static esp_err_t get(httpd_req_t *req){
 if(!authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Request token required");
 cJSON *root=cJSON_CreateObject(),*array=cJSON_AddArrayToObject(root,"lines");
 if(!root||!array){cJSON_Delete(root);return ESP_FAIL;}
 if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE){cJSON_Delete(root);return ESP_FAIL;}
 for(unsigned i=0;i<DIAG_LINES;i++)if(lines[i].text[0]){cJSON *row=cJSON_CreateObject();cJSON_AddStringToObject(row,"raw",lines[i].text);cJSON_AddNumberToObject(row,"received_uptime_ms",(double)lines[i].at);cJSON_AddItemToArray(array,row);}
 xSemaphoreGive(mutex);
 char *json=cJSON_PrintUnformatted(root);cJSON_Delete(root);if(!json)return ESP_FAIL;httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");esp_err_t err=httpd_resp_sendstr(req,json);free(json);return err;
}
static esp_err_t post(httpd_req_t *req){if(!authorized(req))return httpd_resp_send_err(req,HTTPD_403_FORBIDDEN,"Request token required");if(req->content_len)return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Empty request required");if(!mutex||xSemaphoreTake(mutex,pdMS_TO_TICKS(100))!=pdTRUE)return ESP_FAIL;probe=true;xSemaphoreGive(mutex);httpd_resp_set_type(req,"application/json");return httpd_resp_sendstr(req,"{\"ok\":true}");}
void gnss_diagnostics_register(httpd_handle_t server){const httpd_uri_t routes[]={{.uri="/api/gnss",.method=HTTP_GET,.handler=get},{.uri="/api/gnss",.method=HTTP_POST,.handler=post}};for(unsigned i=0;i<2;i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));}
