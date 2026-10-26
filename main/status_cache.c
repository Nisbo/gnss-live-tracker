#include "status_cache.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
typedef struct {char *data;size_t length;} writer_t;
static void put(writer_t *w,const char *s,size_t n){if(w->data)memcpy(w->data+w->length,s,n);w->length+=n;}
static void literal(writer_t *w,const char *s){put(w,s,strlen(s));}
static void integer(writer_t *w,unsigned n){char b[12];int len=snprintf(b,sizeof(b),"%u",n);put(w,b,len);}
static void bytes(writer_t *w,const uint8_t *b,size_t n){literal(w,"[");for(size_t i=0;i<n;i++){if(i)literal(w,",");integer(w,b[i]);}literal(w,"]");}
static void render(writer_t *w,const char *metadata,const uint8_t *position,const tracker_sat_snapshot_t *sat){
 put(w,metadata,strlen(metadata)-1);literal(w,strlen(metadata)>2?",\"position_payload\":":"\"position_payload\":");bytes(w,position,POSITION_BYTES);
 literal(w,",\"satellite_packets\":[");unsigned count=sat->count?(sat->count+SATS_PER_PACKET-1)/SATS_PER_PACKET:1;
 for(unsigned i=0;i<count;i++){uint8_t packet[51];size_t n=tracker_sat_packet(sat,i,packet);if(i)literal(w,",");bytes(w,packet,n);}literal(w,"],\"sky\":[");
 for(unsigned i=0;i<sat->count;i++){const tracker_sat_view_t *s=&sat->satellites[i];if(i)literal(w,",");literal(w,"[");integer(w,s->system);literal(w,",");integer(w,s->id);literal(w,",");
  if(s->elevation<=90)integer(w,s->elevation);else literal(w,"null");literal(w,",");
  if(s->azimuth<360)integer(w,s->azimuth);else literal(w,"null");literal(w,",");
  if(s->cn0!=255)integer(w,s->cn0);else literal(w,"null");literal(w,",");integer(w,s->flags|((sat->usage_known_mask&(1u<<(s->system-1)))?4:0));literal(w,"]");
 }literal(w,"]}");
}
status_blob_t *status_build(const char *metadata,const uint8_t position[POSITION_BYTES],const tracker_sat_snapshot_t *sat){
 if(!metadata||metadata[0]!='{'||strlen(metadata)<2||metadata[strlen(metadata)-1]!='}'||!position||!sat||sat->count>TRACKER_MAX_SATS)return NULL;
 for(unsigned i=0;i<sat->count;i++)if(sat->satellites[i].system<1||sat->satellites[i].system>7)return NULL;
 writer_t w={0};render(&w,metadata,position,sat);if(w.length>32768)return NULL;
 status_blob_t *b=malloc(sizeof(*b)+w.length+1);if(!b)return NULL;b->length=w.length;b->refs=1;w=(writer_t){.data=b->data};render(&w,metadata,position,sat);b->data[w.length]=0;return b;
}
status_blob_t *status_acquire(status_cache_t *cache){status_blob_t *b=cache->current;if(b)b->refs++;return b;}
void status_release(status_blob_t *b){if(b&&!--b->refs)free(b);}
void status_replace(status_cache_t *cache,status_blob_t *next){status_blob_t *old=cache->current;cache->current=next;status_release(old);}
