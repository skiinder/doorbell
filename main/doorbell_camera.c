#include "doorbell_camera.h"
#include "doorbell_config.h"
#include "esp_check.h"

#define TAG "doorbell_camera"

void doorbell_camera_init()
{
    // 摄像头配置
    camera_config_t camera_config = {
        .pin_pwdn = CAM_PIN_PWDN,
        .pin_reset = CAM_PIN_RESET,
        .pin_xclk = CAM_PIN_XCLK,
        .pin_sccb_sda = CAM_PIN_SIOD,
        .pin_sccb_scl = CAM_PIN_SIOC,

        .pin_d7 = CAM_PIN_D7,
        .pin_d6 = CAM_PIN_D6,
        .pin_d5 = CAM_PIN_D5,
        .pin_d4 = CAM_PIN_D4,
        .pin_d3 = CAM_PIN_D3,
        .pin_d2 = CAM_PIN_D2,
        .pin_d1 = CAM_PIN_D1,
        .pin_d0 = CAM_PIN_D0,
        .pin_vsync = CAM_PIN_VSYNC,
        .pin_href = CAM_PIN_HREF,
        .pin_pclk = CAM_PIN_PCLK,

        .xclk_freq_hz = 10000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_240X240,

        .jpeg_quality = 3,
        .fb_count = 1,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_LATEST};

    // 初始化摄像头
    ESP_ERROR_CHECK(esp_camera_init(&camera_config));
}

esp_err_t doorbell_camera_getJpgFrame(uint8_t **data, size_t *size)
{
    camera_fb_t *fb = esp_camera_fb_get();
    ESP_RETURN_ON_FALSE(fb, ESP_FAIL, TAG, "Camera capture failed");

    bool jpeg_converted = frame2jpg(fb, 80, data, size);
    esp_camera_fb_return(fb);
    ESP_RETURN_ON_FALSE(jpeg_converted, ESP_FAIL, TAG, "JPEG conversion failed");

    return ESP_OK;
}

void doorbell_camera_freeJpgFrame(uint8_t *data)
{
    free(data);
}

void doorbell_camera_deinit()
{
    ESP_ERROR_CHECK(esp_camera_deinit());
}
