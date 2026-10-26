#include "sat_import.h"
#include <assert.h>
#include <stdio.h>
static bool parse(const char *s,sat_record_t *out){cJSON *j=cJSON_Parse(s);bool ok=sat_import_parse(j,out);cJSON_Delete(j);return ok;}
int main(void){sat_record_t out;const char *valid="{\"t\":1792670400123,\"up\":12.3,\"boot\":42,\"time_source\":\"gnss\",\"time_confirmed\":true,\"time_estimated\":true,\"fix\":false,\"total\":null,\"v\":[12,8,10,13,0,5,null],\"u\":[8,4,5,10,0,null,null]}";
 assert(parse(valid,&out)&&out.utc_s==1792670400&&out.total==255&&out.visible[6]==255&&out.used[5]==255&&!(out.flags&2)&&(out.flags&SAT_IMPORTED)&&(out.flags&SAT_TIME_DERIVED));
 cJSON *j=cJSON_Parse(valid);cJSON_ReplaceItemInObject(j,"time_source",cJSON_CreateString("browser_legacy"));assert(!sat_import_parse(j,&out));cJSON_ReplaceItemInObject(j,"time_source",cJSON_CreateString("gnss"));cJSON_ReplaceItemInObject(j,"time_confirmed",cJSON_CreateFalse());assert(!sat_import_parse(j,&out));cJSON_ReplaceItemInObject(j,"time_confirmed",cJSON_CreateTrue());cJSON_ReplaceItemInArray(cJSON_GetObjectItem(j,"v"),0,cJSON_CreateNumber(97));assert(!sat_import_parse(j,&out));cJSON_Delete(j);
 assert(!parse("null",&out)&&!parse("{}",&out));puts("Satellite import: confirmed GNSS only, no-fix/unknown counts, derived UTC, range/type rejection passed");}
