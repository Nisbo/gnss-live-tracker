#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include "track_ring.h"
#define BACKUP_HEADER_MAX 12288
#define BACKUP_CHUNK 16384
#define BACKUP_TRACK_BYTES 0x300000u
#define BACKUP_HISTORY_BYTES 0x80000u
static inline uint32_t backup_crc(uint32_t crc,const void *data,size_t n){const uint8_t *p=data;crc=~crc;for(size_t i=0;i<n;i++){crc^=p[i];for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}return ~crc;}
static inline bool backup_chunk_valid(unsigned length,unsigned offset,unsigned n){return n&&n<=BACKUP_CHUNK&&!(offset%4096)&&!(n%4096)&&offset<=length&&n<=length-offset;}
static inline bool backup_hex_decode(const char *text,uint8_t *out,size_t n){if(!text||strlen(text)!=n*2)return false;for(size_t i=0;i<n;i++){unsigned byte=0;for(unsigned j=0;j<2;j++){unsigned char c=text[i*2+j];int v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(v<0)return false;byte=byte*16+v;}out[i]=byte;}return true;}
static inline void backup_hex_encode(const uint8_t *data,size_t n,char *out){static const char h[]="0123456789ABCDEF";for(size_t i=0;i<n;i++){out[2*i]=h[data[i]>>4];out[2*i+1]=h[data[i]&15];}out[n*2]=0;}

static inline bool backup_apply_chunk(track_io_t io,unsigned length,unsigned offset,unsigned n,unsigned current,const uint8_t *data,uint32_t crc,uint32_t *next){
 if(!data||!next||!io.erase||!io.write||!backup_chunk_valid(length,offset,n)||offset>current||backup_crc(0,data,n)!=crc)return false;
 if(!io.erase(io.context,offset,n)||!io.write(io.context,offset,data,n))return false;
 *next=current>offset+n?current:offset+n;return true;
}
