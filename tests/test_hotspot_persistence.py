"""Exercise the production hotspot NVS path without changing Wi-Fi hardware."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'main/tracker_web.c').read_text()
start=source.index('static esp_err_t save_wifi_mode(')
end=source.index('\n}',source.index('static void load_wifi(',start))+2
functions=source[start:end]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PROFILE_COUNT 8
#define TRACKER_SETUP_SSID "Example Setup"
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef struct {char ssid[33],password[64];} saved_profile_t;
typedef struct {uint32_t version;saved_profile_t profiles[8];char ap_ssid[33],ap_password[64];} saved_wifi_t;
static saved_wifi_t saved,stored;
static uint8_t saved_hotspot_mode,stored_mode;
static bool has_mode,has_profiles,fail_commit;
static unsigned profile_writes;
static int nvs_open(const char *s,int mode,int *h){(void)mode;assert(!strcmp(s,"tracker_web"));*h=1;return 0;}
static void nvs_close(int h){(void)h;}
static int nvs_commit(int h){(void)h;return fail_commit?-1:0;}
static int nvs_set_u8(int h,const char *k,uint8_t m){(void)h;assert(!strcmp(k,"ap_mode"));stored_mode=m;has_mode=true;return 0;}
static int nvs_get_u8(int h,const char *k,uint8_t *m){(void)h;assert(!strcmp(k,"ap_mode"));if(!has_mode)return -1;*m=stored_mode;return 0;}
static int nvs_set_blob(int h,const char *k,const void *p,size_t n){(void)h;assert(!strcmp(k,"profiles_v1")&&n==sizeof(stored));memcpy(&stored,p,n);has_profiles=true;profile_writes++;return 0;}
static int nvs_get_blob(int h,const char *k,void *p,size_t *n){(void)h;assert(!strcmp(k,"profiles_v1"));if(!has_profiles)return -1;assert(*n==sizeof(stored));memcpy(p,&stored,*n);return 0;}
static int nvs_get_str(int h,const char *k,char *p,size_t *n){(void)h;(void)k;(void)p;(void)n;return -1;}
'''+functions+r'''
static void restart(void){memset(&saved,0,sizeof(saved));saved_hotspot_mode=0;load_wifi();}
int main(void){
 restart();assert(saved_hotspot_mode==0);
 strcpy(saved.profiles[0].ssid,"Example Wi-Fi");strcpy(saved.profiles[0].password,"synthetic-password");assert(!save_wifi(&saved));saved_wifi_t original=stored;
 unsigned before=profile_writes;
 for(int mode=0;mode<3;mode++){assert(!save_wifi_mode(NULL,mode));restart();assert(saved_hotspot_mode==mode);assert(!memcmp(&saved,&original,sizeof(saved)));}
 assert(profile_writes==before);
 stored_mode=3;restart();assert(saved_hotspot_mode==0);
 has_mode=false;restart();assert(saved_hotspot_mode==0);
 fail_commit=true;assert(save_wifi_mode(NULL,2)!=ESP_OK);
 puts("Hotspot production storage: three persistent modes, old-key default, invalid-mode rejection, commit error and unchanged profile bytes PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='tracker-hotspot-') as tmp:
    c=Path(tmp)/'test.c';binary=Path(tmp)/'test';c.write_text(code)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
