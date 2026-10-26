#pragma once
#include "esp_http_server.h"
#define ESP_PARTITION_TYPE_DATA 1
typedef unsigned esp_partition_subtype_t;
typedef struct {unsigned address,size;uint8_t *memory;} esp_partition_t;
const esp_partition_t *esp_partition_find_first(unsigned,unsigned,const char *);
esp_err_t esp_partition_read(const esp_partition_t *,size_t,void *,size_t);
esp_err_t esp_partition_write(const esp_partition_t *,size_t,const void *,size_t);
esp_err_t esp_partition_erase_range(const esp_partition_t *,size_t,size_t);
