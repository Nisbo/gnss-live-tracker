#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
typedef struct {uint8_t version,reset_session,reserved[2],dev_eui[8],join_eui[8],app_key[16];} tracker_credentials_t;
_Static_assert(sizeof(tracker_credentials_t)==36,"credential record");
static inline int credential_digit(unsigned char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static inline bool credential_parse(const char *s,uint8_t *out,unsigned length){
 if(!s||!out)return false;
 uint8_t parsed[16]={0};unsigned digits=0;
 for(;*s;s++){if(*s==' '||*s==':'||*s=='-'||*s=='\t'||*s=='\r'||*s=='\n')continue;int n=credential_digit(*s);if(n<0||digits>=length*2||length>16)return false;if(!(digits&1))parsed[digits/2]=n<<4;else parsed[digits/2]|=n;digits++;}
 if(digits!=length*2)return false;
 memcpy(out,parsed,length);memset(parsed,0,sizeof(parsed));return true;
}
static inline void credential_hex(const uint8_t *in,unsigned length,char *out){static const char hex[]="0123456789ABCDEF";for(unsigned i=0;i<length;i++){out[i*2]=hex[in[i]>>4];out[i*2+1]=hex[in[i]&15];}out[length*2]=0;}
static inline bool credential_equal(const tracker_credentials_t *a,const tracker_credentials_t *b){return !memcmp(a->dev_eui,b->dev_eui,8)&&!memcmp(a->join_eui,b->join_eui,8)&&!memcmp(a->app_key,b->app_key,16);}
