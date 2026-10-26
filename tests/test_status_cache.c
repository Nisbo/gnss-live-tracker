#include "status_cache.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static status_cache_t cache;
static const uint8_t pos[POSITION_BYTES]={2};
static tracker_sat_snapshot_t empty;
static void *reader(void *unused){(void)unused;for(int i=0;i<2000;i++){pthread_mutex_lock(&mutex);status_blob_t *b=status_acquire(&cache);pthread_mutex_unlock(&mutex);if(b){assert(b->data[b->length]==0);assert(strstr(b->data,"\"position_payload\":[2,0,"));assert(b->data[b->length-1]=='}');pthread_mutex_lock(&mutex);status_release(b);pthread_mutex_unlock(&mutex);}}return NULL;}
int main(void){
 status_blob_t *a=status_build("{}",pos,&empty);assert(a&&!strncmp(a->data,"{\"position_payload\":",20));status_replace(&cache,a);status_blob_t *held=status_acquire(&cache);status_replace(&cache,status_build("{\"generation\":2}",pos,&empty));assert(held->refs==1&&!strstr(held->data,"generation"));status_release(held);
 pthread_t t[2];for(int i=0;i<2;i++)assert(!pthread_create(&t[i],NULL,reader,NULL));
 for(int i=0;i<1000;i++){status_blob_t *next=status_build("{\"generation\":3}",pos,&empty);assert(next);pthread_mutex_lock(&mutex);status_replace(&cache,next);pthread_mutex_unlock(&mutex);}for(int i=0;i<2;i++)pthread_join(t[i],NULL);
 status_replace(&cache,NULL);assert(!status_acquire(&cache));
 tracker_sat_snapshot_t full={.count=TRACKER_MAX_SATS,.sequence=65535,.usage_known_mask=127};
 for(unsigned i=0;i<full.count;i++)full.satellites[i]=(tracker_sat_view_t){.system=1+i%7,.id=1000+i,.azimuth=i%2?65535:359,.elevation=i%2?255:90,.cn0=i%2?255:99,.flags=3};
 a=status_build("{\"metadata\":\"escaped: \\\" and \\\\ \\n\"}",pos,&full);assert(a&&strlen(a->data)==a->length&&a->length<8192);puts(a->data);status_release(a);
 assert(!status_build("not JSON",pos,&empty));full.count=97;assert(!status_build("{}",pos,&full));
 fprintf(stderr,"Status cache: retained readers, replacement concurrency, empty metadata and maximum satellite JSON passed\n");
}
