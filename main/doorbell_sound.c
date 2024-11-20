#include "doorbell_sound.h"
#include "doorbell_config.h"
#include "esp_check.h"
#include "esp_err.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_audio_enc.h"
#include "esp_audio_enc_default.h"
#include "esp_audio_dec.h"
#include "esp_audio_dec_default.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"

#define TAG "doorbell_sound"
#define ES8311_VOLUME 80
#define ES8311_SAMPLE_RATE 16000
#define ES8311_MCLK_MULTIPLE I2S_MCLK_MULTIPLE_256
#define ES8311_MCLK_FREQ_HZ (ES8311_SAMPLE_RATE * ES8311_MCLK_MULTIPLE)

#define MIC_MESSAGE_LEN 2040

static void doorbell_board_i2c_init(i2c_master_bus_handle_t *i2c_bus_handle)
{
    i2c_master_bus_config_t i2c_bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .scl_io_num = I2C_SCL_IO,
        .sda_io_num = I2C_SDA_IO,
        .i2c_port = I2C_NUM,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_new_master_bus(&i2c_bus_cfg, i2c_bus_handle);
}
static void doorbell_sound_i2s_init(i2s_chan_handle_t *speaker_handle, i2s_chan_handle_t *mic_handle)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, speaker_handle, mic_handle));
    i2s_std_config_t std_cfg =
        {
            .clk_cfg = {
                .clk_src = I2S_CLK_SRC_DEFAULT,
                .mclk_multiple = ES8311_MCLK_MULTIPLE,
                .sample_rate_hz = ES8311_SAMPLE_RATE,
            },
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(ESP_AUDIO_BIT16, ESP_AUDIO_MONO),
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

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*speaker_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(*mic_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(*speaker_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(*mic_handle));
}

static void doorbell_sound_codec_init(i2s_chan_handle_t speaker_handle, i2s_chan_handle_t mic_handle, i2c_master_bus_handle_t i2c_bus_handle, doorbell_sound_handle_t sound)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .rx_handle = mic_handle,
        .tx_handle = speaker_handle,
        .port = I2S_NUM,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .port = I2C_NUM,
        .bus_handle = i2c_bus_handle,
    };
    const audio_codec_ctrl_if_t *out_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(out_ctrl_if);

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    assert(gpio_if);

    es8311_codec_cfg_t es8311_cfg = {
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .ctrl_if = out_ctrl_if,
        .gpio_if = gpio_if,
        .pa_pin = PA_PIN,
        .pa_reverted = false,
        .use_mclk = true,
        .mclk_div = ES8311_MCLK_MULTIPLE,
        .digital_mic = false,
    };
    const audio_codec_if_t *out_codec_if = es8311_codec_new(&es8311_cfg);
    assert(out_codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .codec_if = out_codec_if,              // es8311_codec_new 获取到的接口实现
        .data_if = data_if,                    // audio_codec_new_i2s_data 获取到的数据接口实现
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, // 设备同时支持录制和播放
    };
    sound->codec_dev = esp_codec_dev_new(&dev_cfg);
    assert(sound->codec_dev);
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(sound->codec_dev, 60.0));
    sound->sample_info.bits_per_sample = ESP_AUDIO_BIT16;
    sound->sample_info.channel = ESP_AUDIO_MONO;
    sound->sample_info.sample_rate = ES8311_SAMPLE_RATE;
    sound->sample_info.mclk_multiple = ES8311_MCLK_MULTIPLE;
    sound->sample_info.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0);
    ESP_ERROR_CHECK(esp_codec_dev_open(sound->codec_dev, &sound->sample_info));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(sound->codec_dev, 30.0));
}

// static void doorbell_sound_encoder_init()
// {
//     ESP_ERROR_CHECK(esp_aac_enc_register());
//     ESP_ERROR_CHECK(esp_aac_dec_register());
//     esp_aac_enc_config_t cfg = {
//         .sample_rate = ES8311_SAMPLE_RATE,
//         .bits_per_sample = ESP_AUDIO_BIT16,
//         .bitrate = 64000,
//         .channel = ESP_AUDIO_DUAL,
//         .adts_used = true,
//     };
// }
static void mic_sync_task(void *args)
{
    doorbell_sound_obj *sound = args;
    void *buf = malloc(MIC_MESSAGE_LEN);
    if (!buf)
    {
        goto MIC_TASK_FAIL;
    }

    while (true)
    {
        if (esp_codec_dev_read(sound->codec_dev, buf, MIC_MESSAGE_LEN) == ESP_CODEC_DEV_OK)
        {
            xRingbufferSend(sound->mic_ringbuf, buf, MIC_MESSAGE_LEN, portMAX_DELAY);
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
    void *buf;

    while (true)
    {
        len = 0;
        buf = xRingbufferReceive(sound->speaker_ringbuf, &len, portMAX_DELAY);
        if (buf)
        {
            ESP_ERROR_CHECK(esp_codec_dev_write(sound->codec_dev, buf, len));
            vRingbufferReturnItem(sound->speaker_ringbuf, buf);
        }
        else
        {
            vTaskDelay(1);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t doorbell_sound_init(doorbell_sound_handle_t *handle,
                              RingbufHandle_t mic_ringbuf,
                              RingbufHandle_t speaker_ringbuf)
{
    doorbell_sound_obj *sound = malloc(sizeof(doorbell_sound_obj));
    ESP_RETURN_ON_FALSE(sound, ESP_ERR_NO_MEM, TAG, "doorbell_sound_handle malloc failed");
    *handle = sound;
    i2s_chan_handle_t speaker_handle = NULL, mic_handle = NULL;
    i2c_master_bus_handle_t i2c_bus = NULL;
    // 使能功放
    doorbell_board_i2c_init(&i2c_bus);
    doorbell_sound_i2s_init(&speaker_handle, &mic_handle);
    assert(mic_handle);
    assert(speaker_handle);
    doorbell_sound_codec_init(speaker_handle, mic_handle, i2c_bus, sound);
    sound->mic_ringbuf = mic_ringbuf;
    sound->speaker_ringbuf = speaker_ringbuf;

    return ESP_OK;
}

void doorbell_sound_start(doorbell_sound_handle_t handle)
{
    assert(xTaskCreate(speaker_sync_task, "speaker_task", 4096, handle, 1, &handle->speaker_sync_task) == pdTRUE);
    assert(xTaskCreate(mic_sync_task, "mic_task", 4096, handle, 1, &handle->mic_sync_task) == pdTRUE);
}

void doorbell_sound_stop(doorbell_sound_handle_t handle)
{
    vTaskDelete(handle->speaker_sync_task);
    vTaskDelete(handle->mic_sync_task);
}

void doorbell_sound_deinit(doorbell_sound_handle_t handle)
{
}
