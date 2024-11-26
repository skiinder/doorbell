#include "esp_log.h"
#include "doorbell_camera.h"
#include "freertos/ringbuf.h"
#include "doorbell_sound.h"
#include "doorbell_wifi.h"
#include "freertos/queue.h"
#include "doorbell_wsclient.h"
#include "doorbell_mqtt.h"

static doorbell_sound_handle_t doorbell_sound;

static RingbufHandle_t mic_ringbuf;
static RingbufHandle_t speaker_ringbuf;

static void wifi_connect_callback(void)
{
    doorbell_wsclient_init(mic_ringbuf, speaker_ringbuf);
    doorbell_mqtt_init();
}

static void wifi_disconnect_callback(void)
{
}

void app_main()
{
    ESP_LOGI("main", "Booting...");

    // 初始化队列
    mic_ringbuf = xRingbufferCreate(65536, RINGBUF_TYPE_NOSPLIT);
    speaker_ringbuf = xRingbufferCreate(65536, RINGBUF_TYPE_NOSPLIT);

    assert(mic_ringbuf);
    assert(speaker_ringbuf);


    // 初始化WiFi
    doorbell_wifi_init(wifi_connect_callback, wifi_disconnect_callback);

    // 初始化server和sound
    ESP_ERROR_CHECK(doorbell_sound_init(&doorbell_sound, mic_ringbuf, speaker_ringbuf));
    // 初始化相机
    doorbell_camera_init();

    doorbell_sound_start(doorbell_sound);
    doorbell_wifi_start();
}