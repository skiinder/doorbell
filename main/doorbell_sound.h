#if !defined(__DOORBELL_SOUND_H__)
#define __DOORBELL_SOUND_H__

#include "driver/i2s_std.h"
#include "doorbell_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct
{
    TaskHandle_t speaker_sync_task;
    TaskHandle_t mic_sync_task;
    i2s_chan_handle_t mic_handle;     // i2s读channel，用于采集mic声音
    i2s_chan_handle_t speaker_handle; // i2s写channel，用于播放声音
    doorbell_buffer_handle_t mic_buffer;       // rx_handle对接的缓存，用于储存从mic采集的声音
    doorbell_buffer_handle_t speaker_buffer;   // tx_handle对接的缓存，用于储存准备播放的声音
} doorbell_sound_obj;

typedef doorbell_sound_obj *doorbell_sound_handle_t;

esp_err_t doorbell_sound_init(doorbell_sound_handle_t *handle,
                              doorbell_buffer_handle_t mic_buffer,
                              doorbell_buffer_handle_t speaker_buffer);
void doorbell_sound_start(doorbell_sound_handle_t handle);
void doorbell_sound_stop(doorbell_sound_handle_t handle);
void doorbell_sound_deinit(doorbell_sound_handle_t handle);

#endif // __DOORBELL_SOUND_H__
