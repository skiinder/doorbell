#include "esp_log.h"
#include "doorbell_camera.h"
#include "doorbell_buffer.h"
#include "doorbell_httpd.h"
#include "doorbell_sound.h"
#include "doorbell_queue.h"
#include "doorbell_wifi.h"
#include "freertos/queue.h"

static doorbell_httpd_handle_t doorbell_server;
static doorbell_sound_handle_t doorbell_sound;

static doorbell_buffer_handle_t mic_buffer;
static doorbell_buffer_handle_t speaker_buffer;

static void wifi_connect_callback(void)
{
    doorbell_httpd_start(doorbell_server);
}

static void wifi_disconnect_callback(void)
{
    doorbell_httpd_stop(doorbell_server);
}

void app_main()
{
    ESP_LOGI("main", "Booting...");

    // 初始化队列
    doorbell_buffer_init(&mic_buffer, 16384);
    doorbell_buffer_init(&speaker_buffer, 16384);

    // 初始化相机
    doorbell_camera_init();

    // 初始化WiFi
    doorbell_wifi_init(wifi_connect_callback, wifi_disconnect_callback);

    // 初始化server和sound
    ESP_ERROR_CHECK(doorbell_httpd_init(&doorbell_server, mic_buffer, speaker_buffer));
    ESP_ERROR_CHECK(doorbell_sound_init(&doorbell_sound, mic_buffer, speaker_buffer));

    doorbell_sound_start(doorbell_sound);
    doorbell_wifi_start();
}