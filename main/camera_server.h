#if !defined(__CAMERA_SERVER_H__)
#define __CAMERA_SERVER_H__

#include "esp_http_server.h"
#include "driver/i2s_std.h"
#include "freertos/event_groups.h"
#include "buffer.h"

typedef struct
{
    httpd_handle_t http_server; // http服务器
    int ws_fd;
    TaskHandle_t ws_sync_task;
    TaskHandle_t speaker_sync_task;
    TaskHandle_t mic_sync_task;
    i2s_chan_handle_t mic_handle; // i2s读channel，用于采集mic声音
    i2s_chan_handle_t speaker_handle; // i2s写channel，用于播放声音
    buffer_handle_t mic_buffer;   // rx_handle对接的缓存，用于储存从mic采集的声音
    buffer_handle_t speaker_buffer;   // tx_handle对接的缓存，用于储存准备播放的声音
    bool talking;
} camera_server_t;

esp_err_t camera_server_init(camera_server_t *camera_server);

esp_err_t camera_server_deinit(camera_server_t *camera_server);

#endif // __CAMERA_SERVER_H__
