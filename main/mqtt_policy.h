#pragma once
#include <stdbool.h>
#include <string.h>
#include "cJSON.h"
static inline bool mqtt_settings_valid(const char *host,const char *topic,unsigned port,unsigned interval,bool enabled){
 if(!port||port>65535||interval<5||interval>3600||!topic[0]||strstr(topic,"//")||strpbrk(topic,"+# \t\r\n")||topic[0]=='/'||topic[strlen(topic)-1]=='/')return false;
 if(enabled&&!host[0])return false;
 /* Hostname/IP only: credentials and ports have separate fields. IPv6 colons are allowed. */
 if(strpbrk(host,"/@?# \t\r\n"))return false;
 return true;
}
static inline bool mqtt_command_parse(const char *body,int *enabled,int *interval){
 cJSON *j=cJSON_Parse(body);bool valid=cJSON_IsObject(j);cJSON *on=cJSON_GetObjectItemCaseSensitive(j,"recording"),*period=cJSON_GetObjectItemCaseSensitive(j,"interval_s");
 if(!on&&!period)valid=false;
 for(cJSON *v=j?j->child:NULL;v;v=v->next)if(!v->string||(strcmp(v->string,"recording")&&strcmp(v->string,"interval_s")))valid=false;
 if(on&&!cJSON_IsBool(on))valid=false;
 if(period&&(!cJSON_IsNumber(period)||period->valuedouble!=period->valueint||period->valueint<5||period->valueint>3600))valid=false;
 if(valid){*enabled=on?cJSON_IsTrue(on):-1;*interval=period?period->valueint:0;}cJSON_Delete(j);return valid;
}
