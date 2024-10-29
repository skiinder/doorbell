#include "doorbell_sound.h"
#include "doorbell_config.h"
#include "es8311.h"
#include "esp_check.h"
#include "esp_err.h"
#include "doorbell_queue.h"
#include "freertos/task.h"

#define TAG "doorbell_sound"
#define ES8311_VOLUME 80
#define ES8311_SAMPLE_RATE 16000
#define ES8311_MCLK_MULTIPLE I2S_MCLK_MULTIPLE_256
#define ES8311_MCLK_FREQ_HZ (ES8311_SAMPLE_RATE * ES8311_MCLK_MULTIPLE)

#define MIC_MESSAGE_LEN 2048

static void doorbell_sound_pa_enable()
{
    gpio_config_t ic = {
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << 46),
    };
    ESP_ERROR_CHECK(gpio_config(&ic));
    ESP_ERROR_CHECK(gpio_set_level(46, 1));
}

static void doorbell_sound_i2s_init(doorbell_sound_obj *sound)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &sound->speaker_handle, &sound->mic_handle));
    i2s_std_config_t std_cfg =
        {
            .clk_cfg = {
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple = ES8311_MCLK_MULTIPLE,
                .sample_rate_hz = ES8311_SAMPLE_RATE},

            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {
                .mclk = I2S_MCK_IO,
                .bclk = I2S_BCK_IO,
                .ws = I2S_WS_IO,
                .dout = I2S_DO_IO,
                .din = I2S_DI_IO,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(sound->speaker_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(sound->mic_handle, &std_cfg));
}

static void doorbell_sound_es8311_init()
{
    // 初始化IIC设备
    const i2c_config_t es8311_i2c_cfg = {
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM, &es8311_i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM, I2C_MODE_MASTER, 0, 0, 0));

    // 初始化es8311芯片
    es8311_handle_t es_handle = es8311_create(I2C_NUM, ES8311_ADDRRES_0);
    assert(es_handle);
    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = ES8311_MCLK_FREQ_HZ,
        .sample_frequency = ES8311_SAMPLE_RATE};

    ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_ERROR_CHECK(es8311_sample_frequency_config(es_handle, ES8311_MCLK_FREQ_HZ, ES8311_SAMPLE_RATE));
    ESP_ERROR_CHECK(es8311_voice_volume_set(es_handle, ES8311_VOLUME, NULL));
    ESP_ERROR_CHECK(es8311_microphone_config(es_handle, false));
    ESP_ERROR_CHECK(es8311_microphone_gain_set(es_handle, ES8311_MIC_GAIN_6DB));
}
static void mic_sync_task(void *args)
{
    size_t len;
    doorbell_sound_obj *sound = args;
    void *buf = malloc(MIC_MESSAGE_LEN);
    if (!buf)
    {
        goto MIC_TASK_FAIL;
    }

    while (true)
    {
        if (i2s_channel_read(sound->mic_handle, buf, MIC_MESSAGE_LEN, &len, 1000) == ESP_OK)
        {
            doorbell_buffer_write(sound->mic_buffer, buf, len);
        }
        else
        {
            vTaskDelay(1);
        }
    }
MIC_TASK_FAIL:
    vTaskDelete(NULL);
}

static void speaker_sync_task(void *args)
{
    doorbell_sound_obj *sound = args;
    size_t len;
    void *buf = malloc(MIC_MESSAGE_LEN);
    if (!buf)
    {
        goto SPEAKER_TASK_FAIL;
    }

    while (true)
    {
        len = doorbell_buffer_read(sound->speaker_buffer, buf, MIC_MESSAGE_LEN);
        if (len > 0)
        {
            ESP_ERROR_CHECK(i2s_channel_write(sound->speaker_handle, buf, len, &len, 1000));
        } else {
            vTaskDelay(1);
        }
    }
SPEAKER_TASK_FAIL:
    vTaskDelete(NULL);
}

esp_err_t doorbell_sound_init(doorbell_sound_handle_t *handle,
                              doorbell_buffer_handle_t mic_buffer,
                              doorbell_buffer_handle_t speaker_buffer)
{
    doorbell_sound_obj *sound = malloc(sizeof(doorbell_sound_obj));
    ESP_RETURN_ON_FALSE(sound, ESP_ERR_NO_MEM, TAG, "doorbell_sound_handle malloc failed");
    *handle = sound;
    // 使能功放
    doorbell_sound_pa_enable();
    doorbell_sound_i2s_init(sound);
    doorbell_sound_es8311_init();
    sound->mic_buffer = mic_buffer;
    sound->speaker_buffer = speaker_buffer;

    return ESP_OK;
}

void doorbell_sound_start(doorbell_sound_handle_t handle)
{
    ESP_ERROR_CHECK(i2s_channel_enable(handle->speaker_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(handle->mic_handle));
    assert(xTaskCreate(speaker_sync_task, "speaker_task", 4096, handle, 1, &handle->speaker_sync_task) == pdTRUE);
    assert(xTaskCreate(mic_sync_task, "mic_task", 4096, handle, 1, &handle->mic_sync_task) == pdTRUE);
}

void doorbell_sound_stop(doorbell_sound_handle_t handle)
{
    vTaskDelete(handle->speaker_sync_task);
    vTaskDelete(handle->mic_sync_task);
    ESP_ERROR_CHECK(i2s_channel_disable(handle->speaker_handle));
    ESP_ERROR_CHECK(i2s_channel_disable(handle->mic_handle));
}

void doorbell_sound_deinit(doorbell_sound_handle_t handle)
{
}
