from test_support import ROOT, CJSON, tmp
"""Compile the actual history GET handler with a host HTTP sink, without hardware."""
from pathlib import Path
import subprocess
root=ROOT
source=(root/'main/satellite_log.c').read_text()
handler=source[source.index('static bool arg('):source.index('static esp_err_t post(')]
prefix=r'''
#include "sat_ring.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
static int mutex=1;
static bool xSemaphoreTake(int m,int t){(void)m;(void)t;return true;}
static void xSemaphoreGive(int m){(void)m;}
static sat_ring_t ring;
static uint8_t flash[4*4096],scratch[4096];static sat_block_t blocks[4];static sat_order_t order[4];
static char response[80000];static size_t length,chunks;
static bool rd(void *c,size_t o,void *b,size_t n){(void)c;assert(o+n<=sizeof(flash));memcpy(b,flash+o,n);return true;}
static bool wr(void *c,size_t o,const void *b,size_t n){(void)c;assert(o+n<=sizeof(flash));memcpy(flash+o,b,n);return true;}
static bool er(void *c,size_t o,size_t n){(void)c;assert(o+n<=sizeof(flash));memset(flash+o,255,n);return true;}
esp_err_t httpd_resp_send_err(httpd_req_t *r,int status,const char *text){(void)text;r->status=status;return ESP_FAIL;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){(void)r;(void)s;return ESP_OK;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *k,const char *v){(void)r;(void)k;(void)v;return ESP_OK;}
esp_err_t httpd_resp_send_chunk(httpd_req_t *r,const char *p,size_t n){(void)r;assert(n<=1024&&length+n<sizeof(response));if(p){memcpy(response+length,p,n);length+=n;}response[length]=0;chunks++;return ESP_OK;}
static esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r,const char *p){return httpd_resp_send_chunk(r,p,strlen(p));}
size_t httpd_req_get_url_query_len(httpd_req_t *r){return strlen(r->body);}
esp_err_t httpd_req_get_url_query_str(httpd_req_t *r,char *out,size_t n){snprintf(out,n,"%s",r->body);return ESP_OK;}
esp_err_t httpd_query_key_value(const char *q,const char *key,char *out,size_t n){size_t k=strlen(key);while(*q){const char *end=strchr(q,'&');if(!end)end=q+strlen(q);if((size_t)(end-q)>k&&q[k]=='='&&!strncmp(q,key,k)){size_t size=end-q-k-1;if(size>=n)return ESP_ERR_INVALID_ARG;memcpy(out,q+k+1,size);out[size]=0;return ESP_OK;}q=*end?end+1:end;}return ESP_ERR_NOT_FOUND;}
'''
suffix=r'''
static cJSON *query(const char *q,int expected){httpd_req_t req={.body=q,.status=200};length=chunks=0;get(&req);assert(req.status==expected);if(expected!=200)return NULL;cJSON *j=cJSON_Parse(response);assert(j);return j;}
int main(void){
 memset(flash,255,sizeof(flash));assert(sat_ring_mount(&ring,(track_io_t){.read=rd,.write=wr,.erase=er},blocks,4,scratch,1)&&sat_ring_index(&ring,order));
 sat_record_t p={.boot=42,.visible={12,10,255,4},.used={8,4,255,2},.total=14,.flags=6};assert(sat_ring_append(&ring,&p));for(unsigned i=0;i<300;i++){p.utc_s=1793000000+i*10;assert(sat_ring_append(&ring,&p));}
 for(unsigned i=0;i<20;i++){p.utc_s=1793000000+i*10;p.flags=SAT_IMPORTED;assert(sat_ring_merge(&ring,&p)==1);}
 cJSON *j=query("order=recent&limit=128",200),*rows=cJSON_GetObjectItem(j,"points");assert(cJSON_GetArraySize(rows)==128&&cJSON_IsTrue(cJSON_GetObjectItem(j,"recent"))&&cJSON_IsTrue(cJSON_GetObjectItem(j,"more")));assert(cJSON_GetObjectItem(cJSON_GetArrayItem(rows,0),"t")->valuedouble==1793002990000.0);assert(chunks<64);
 cJSON *last=cJSON_GetArrayItem(rows,127);uint32_t t=cJSON_GetObjectItem(last,"t")->valuedouble/1000,seq=cJSON_GetObjectItem(last,"seq")->valuedouble;cJSON_Delete(j);
 char q[180];snprintf(q,sizeof(q),"order=recent&before_s=%u&before_seq=%u&limit=128",t,seq);j=query(q,200);rows=cJSON_GetObjectItem(j,"points");cJSON *first=cJSON_GetArrayItem(rows,0);double first_t=cJSON_GetObjectItem(first,"t")->valuedouble;assert(first_t<t*1000.0||(first_t==t*1000.0&&cJSON_GetObjectItem(first,"seq")->valuedouble<seq));cJSON_Delete(j);
 j=query("order=recent&since_s=1793002800&limit=128",200);assert(cJSON_GetArraySize(cJSON_GetObjectItem(j,"points"))==20&&!cJSON_IsTrue(cJSON_GetObjectItem(j,"more")));cJSON_Delete(j);
 j=query("after=0&limit=128",200);assert(!cJSON_IsTrue(cJSON_GetObjectItem(j,"recent")));rows=cJSON_GetObjectItem(j,"points");assert(cJSON_GetObjectItem(cJSON_GetArrayItem(rows,0),"seq")->valueint==1&&cJSON_IsNull(cJSON_GetObjectItem(cJSON_GetArrayItem(rows,0),"t")));cJSON_Delete(j);
 const char *bad[]={"order=oldest","order=recent&before_s=-1","order=recent&before_seq=4294967296","order=recent&since_s=1.5","order=recent&limit=129","order=recent&limit=0"};for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)query(bad[i],400);
 sat_ring_clear(&ring);j=query("order=recent",200);assert(cJSON_GetArraySize(cJSON_GetObjectItem(j,"points"))==0&&!cJSON_IsTrue(cJSON_GetObjectItem(j,"more")));cJSON_Delete(j);
 puts("Production history HTTP: recent cursors/time filter, legacy ascending compatibility, valid buffered JSON, <64 chunks/128 points, bounds and empty clear PASS");
}
'''
p=Path(tmp('tracker-history-http.c'));p.write_text(prefix+handler+suffix)
binary=Path(tmp('tracker-history-http'))
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(root/'main'),'-I'+str(root/'tests/backup_stubs'),'-I'+str(Path(CJSON)),str(p),str(root/'main/sat_ring.c'),str(Path(CJSON)/'cJSON.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
