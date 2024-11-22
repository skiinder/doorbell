#if !defined(__DOORBELL_SOUND_H__)
#define __DOORBELL_SOUND_H__

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "freertos/ringbuf.h"

typedef struct
{
    TaskHandle_t speaker_sync_task;
    TaskHandle_t mic_sync_task;
    esp_codec_dev_handle_t codec_dev;
    esp_codec_dev_sample_info_t sample_info;
    RingbufHandle_t mic_ringbuf;
    RingbufHandle_t speaker_ringbuf;
} doorbell_sound_obj;

typedef doorbell_sound_obj *doorbell_sound_handle_t;

esp_err_t doorbell_sound_init(doorbell_sound_handle_t *handle,
                              RingbufHandle_t mic_ringbuf,
                              RingbufHandle_t speaker_ringbuf);
void doorbell_sound_start(doorbell_sound_handle_t handle);
void doorbell_sound_stop(doorbell_sound_handle_t handle);
void doorbell_sound_deinit(doorbell_sound_handle_t handle);

#endif // __DOORBELL_SOUND_H__
