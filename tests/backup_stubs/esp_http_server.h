#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
typedef int esp_err_t;typedef void *httpd_handle_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_NOT_FOUND 4
#define HTTPD_400_BAD_REQUEST 400
#define HTTPD_403_FORBIDDEN 403
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTP_GET 0
#define HTTP_POST 1
#define ESP_ERROR_CHECK(e) assert((e)==ESP_OK)
typedef struct {const char *uri;size_t content_len,received;const char *body;int status;char response[512];} httpd_req_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t *);} httpd_uri_t;
esp_err_t httpd_resp_send_err(httpd_req_t *,int,const char *);
esp_err_t httpd_resp_sendstr(httpd_req_t *,const char *);
esp_err_t httpd_resp_send_chunk(httpd_req_t *,const char *,size_t);
esp_err_t httpd_resp_set_type(httpd_req_t *,const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *,const char *,const char *);
esp_err_t httpd_resp_set_status(httpd_req_t *,const char *);
size_t httpd_req_get_url_query_len(httpd_req_t *);
esp_err_t httpd_req_get_url_query_str(httpd_req_t *,char *,size_t);
esp_err_t httpd_query_key_value(const char *,const char *,char *,size_t);
int httpd_req_recv(httpd_req_t *,char *,size_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
