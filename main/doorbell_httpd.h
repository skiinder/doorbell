#if !defined(__DOORBELL_HTTPD_H__)
#define __DOORBELL_HTTPD_H__

#include "esp_http_server.h"
#include "doorbell_queue.h"
#include "doorbell_buffer.h"

typedef struct
{
    httpd_handle_t http_server;              // http服务器
    httpd_handle_t stream_server;            // stream服务器
    int ws_fd;                               // web_socket文件描述符
    TaskHandle_t ws_sync_task;               // web_socket后台推送任务
    bool talking;                            // 是否正在通话
    doorbell_buffer_handle_t mic_buffer;     // rx_handle对接的缓存，用于储存从mic采集的声音
    doorbell_buffer_handle_t speaker_buffer; // tx_handle对接的缓存，用于储存准备播放的声音
} doorbell_httpd_obj;

typedef doorbell_httpd_obj *doorbell_httpd_handle_t;

esp_err_t doorbell_httpd_init(doorbell_httpd_handle_t *doorbell_httpd_handle,
                              doorbell_buffer_handle_t mic_buffer,
                              doorbell_buffer_handle_t speaker_buffer);
void doorbell_httpd_start(doorbell_httpd_handle_t doorbell_httpd_handle);
void doorbell_httpd_stop(doorbell_httpd_handle_t doorbell_httpd_handle);
void doorbell_httpd_deinit(doorbell_httpd_handle_t doorbell_httpd_handle);

#endif // __DOORBELL_HTTPD_H__
