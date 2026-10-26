#include "sat_import.h"
#include <math.h>
#include <string.h>
static const cJSON *item(const cJSON *j,const char *key){return cJSON_GetObjectItemCaseSensitive(j,key);}
static bool number(const cJSON *v,double low,double high,bool integral){return cJSON_IsNumber(v)&&isfinite(v->valuedouble)&&v->valuedouble>=low&&v->valuedouble<=high&&(!integral||floor(v->valuedouble)==v->valuedouble);}
static bool count(const cJSON *v,uint8_t *out){if(cJSON_IsNull(v)){*out=255;return true;}if(!number(v,0,96,true))return false;*out=v->valueint;return true;}
bool sat_import_parse(const cJSON *j,sat_record_t *out){
 const cJSON *t=item(j,"t"),*boot=item(j,"boot"),*up=item(j,"up"),*source=item(j,"time_source"),*fix=item(j,"fix"),*confirmed=item(j,"time_confirmed"),*v=item(j,"v"),*u=item(j,"u");
 if(!cJSON_IsObject(j)||!number(t,315532800000LL,4102444799999LL,true)||!number(boot,1,UINT32_MAX,true)||!number(up,0,UINT32_MAX,false)||!cJSON_IsString(source)||strcmp(source->valuestring,"gnss")||!cJSON_IsTrue(confirmed)||!cJSON_IsBool(fix)||!cJSON_IsArray(v)||!cJSON_IsArray(u)||cJSON_GetArraySize(v)!=7||cJSON_GetArraySize(u)!=7)return false;
 sat_record_t p={.utc_s=(uint32_t)(t->valuedouble/1000),.boot=boot->valuedouble,.uptime_s=up->valuedouble,.flags=SAT_IMPORTED|(cJSON_IsTrue(item(j,"time_estimated"))?SAT_TIME_DERIVED:0)|4|(cJSON_IsTrue(fix)?2:0)|(cJSON_IsTrue(item(j,"break"))?1:0)};
 if(!count(item(j,"total"),&p.total))return false;
 for(unsigned i=0;i<7;i++)if(!count(cJSON_GetArrayItem(v,i),&p.visible[i])||!count(cJSON_GetArrayItem(u,i),&p.used[i]))return false;
 *out=p;return true;
}
