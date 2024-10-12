#include "camera_server_config.h"
#include "camera_server.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "esp_camera.h"
#include "es8311.h"
#include "esp_check.h"
#include "esp_timer.h"

#define LOG_TAG "camera_server"
#define ES8311_VOLUME 80
#define ES8311_SAMPLE_RATE 16000
#define ES8311_MCLK_MULTIPLE I2S_MCLK_MULTIPLE_256
#define ES8311_MCLK_FREQ_HZ (ES8311_SAMPLE_RATE * ES8311_MCLK_MULTIPLE)

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t root_handler(httpd_req_t *req)
{
    return ESP_OK;
}

static esp_err_t jpeg_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len;
    uint8_t *_jpg_buf;
    char *part_buf[64];
    static int64_t last_frame = 0;
    if (!last_frame)
    {
        last_frame = esp_timer_get_time();
    }

    // 设置响应内容为流媒体
    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "X-Framerate", "60");
    while (true)
    {
        // 获取图片缓冲区
        fb = esp_camera_fb_get();
        if (!fb)
        {
            res = ESP_FAIL;
            break;
        }
        if (fb->format != PIXFORMAT_JPEG)
        {
            bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
            if (!jpeg_converted)
            {
                esp_camera_fb_return(fb);
                res = ESP_FAIL;
                break;
            }
        }
        else
        {
            _jpg_buf_len = fb->len;
            _jpg_buf = fb->buf;
        }

        // 分段将图片数据返回给浏览器
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY,
                                        strlen(_STREAM_BOUNDARY));
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);

            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (fb->format != PIXFORMAT_JPEG)
        {
            free(_jpg_buf);
        }
        esp_camera_fb_return(fb);
        if (res != ESP_OK)
        {
            break;
        }
        int64_t fr_end = esp_timer_get_time();
        int64_t frame_time = fr_end - last_frame;
        last_frame = fr_end;
        frame_time /= 1000;
    }

    last_frame = 0;
    return res;
}

static void ws_sync_task(void *arg)
{
    camera_server_t *cs = arg;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = malloc(8192);
    if (!ws_pkt.payload)
    {
        ESP_LOGE(LOG_TAG, "Not enough memory for buffer");
        abort();
    }

    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    while (cs->talking)
    {
        // 将mic缓存中的数据发送到浏览器
        ws_pkt.len = buffer_read(cs->mic_buffer, ws_pkt.payload, 8192);
        if (ws_pkt.len > 0)
        {
            httpd_ws_send_frame_async(cs->http_server, cs->ws_fd, &ws_pkt);
        }
        else
        {
            // 喂狗
            vTaskDelay(1);
        }
    }
    vTaskDelete(NULL);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    camera_server_t *cs = req->user_ctx;
    esp_err_t ret = ESP_OK;
    // 在接收到浏览器的请求时，先构建websocket连接
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(LOG_TAG, "Handshake done, the new connection was opened");
        cs->talking = true;
        cs->ws_fd = httpd_req_to_sockfd(req);
        xTaskCreate(ws_sync_task, "ws_task", 4096, cs, 5, &cs->ws_sync_task);
        return ESP_OK;
    }

    // websocket数据的帧格式
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    // websocket数据的帧格式是文本格式
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    /* Set max_len = 0 to get the frame len */
    // 将最后一个参数max_len设置为0，用来获取浏览器通过websocket发来的帧格式的长度
    ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(LOG_TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    ESP_LOGI(LOG_TAG, "frame len is %d", ws_pkt.len);
    if (ws_pkt.len)
    {
        ws_pkt.payload = malloc(ws_pkt.len);
        if (ws_pkt.payload == NULL)
        {
            ESP_LOGE(LOG_TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        /* Set max_len = ws_pkt.len to get the frame payload */
        // 获取浏览器通过websocket发送过来的帧格式
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);

        // 正确获取数据后，将数据发送到缓存
        if (ret == ESP_OK && cs->talking)
        {
            buffer_write(cs->speaker_buffer, ws_pkt.payload, ws_pkt.len);            
        }
        free(ws_pkt.payload);
    }
    return ret;
}

static esp_err_t switch_handler(httpd_req_t *req)
{
    camera_server_t *cs = req->user_ctx;
    assert(cs);
    cs->talking = !cs->talking;
    httpd_resp_send(req, NULL,0);
    return ESP_OK;
}

static void event_handler_on_wifi_start(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(LOG_TAG, "WiFi started, try to connect...");
    ESP_ERROR_CHECK(esp_wifi_connect());
}

static void event_handler_on_ip_got(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(LOG_TAG, "Starting http server...");
    camera_server_t *camera_server = event_handler_arg;
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&camera_server->http_server, &http_config));
}

static void event_handler_on_wifi_disconnected(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGE(LOG_TAG, "Failed to connect to Wifi");
    camera_server_t *camera_server = event_handler_arg;
    ESP_ERROR_CHECK(httpd_unregister_uri(camera_server->http_server, "/"));
    ESP_ERROR_CHECK(httpd_unregister_uri(camera_server->http_server, "/jpeg"));
    ESP_ERROR_CHECK(httpd_unregister_uri(camera_server->http_server, "/ws"));
    ESP_ERROR_CHECK(httpd_unregister_uri(camera_server->http_server, "/switch"));
    ESP_ERROR_CHECK(httpd_stop(camera_server->http_server));
}

static void event_handler_on_http_server_start(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(LOG_TAG, "Http server started, registering uri handlers...");
    camera_server_t *camera_server = event_handler_arg;
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .is_websocket = true,
        .user_ctx = camera_server};
    httpd_uri_t jpeg_uri = {
        .uri = "/jpeg",
        .method = HTTP_GET,
        .is_websocket = true,
        .handler = jpeg_handler,
        .user_ctx = camera_server};
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
        .user_ctx = camera_server};
    httpd_uri_t switch_uri = {
        .uri = "/switch",
        .method = HTTP_GET,
        .is_websocket = true,
        .handler = switch_handler,
        .user_ctx = camera_server};
    ESP_ERROR_CHECK(httpd_register_uri_handler(camera_server->http_server, &root_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(camera_server->http_server, &jpeg_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(camera_server->http_server, &ws_uri));
    ESP_ERROR_CHECK(httpd_register_uri_handler(camera_server->http_server, &switch_uri));
}

static esp_err_t camera_init()
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

        .xclk_freq_hz = 20000000,
        .ledc_timer = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,

        .pixel_format = PIXFORMAT_RGB565,
        .frame_size = FRAMESIZE_240X240,

        .jpeg_quality = 3,
        .fb_count = 1,
        .fb_location = CAMERA_FB_IN_PSRAM,
        .grab_mode = CAMERA_GRAB_WHEN_EMPTY};

    // 初始化摄像头
    return esp_camera_init(&camera_config);
    // return ESP_OK;
}

static esp_err_t wifi_init()
{
    // WIFI配置
    wifi_config_t config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .scan_method = WIFI_FAST_SCAN}

    };

    // 初始化TCP/IP栈
    ESP_RETURN_ON_ERROR(esp_netif_init(), LOG_TAG, "Failed to init netif");

    // 创建wifi网卡
    esp_netif_t *netif_sta = esp_netif_create_default_wifi_sta();
    assert(netif_sta);

    // 初始化wifi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), LOG_TAG, "Failed to init WiFi");

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_RETURN_ON_ERROR(esp_wifi_start(), LOG_TAG, "Failed to start WiFi");
    return ESP_OK;
}

static esp_err_t sound_init(camera_server_t *cs)
{
    // 使能功放
    gpio_config_t ic = {};

    ic.mode = GPIO_MODE_OUTPUT;
    ic.intr_type = GPIO_INTR_DISABLE;
    ic.pin_bit_mask = (1ULL << 46);
    gpio_config(&ic);
    gpio_set_level(46, 1);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &cs->speaker_handle, &cs->mic_handle));
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

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(cs->speaker_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(cs->mic_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(cs->speaker_handle));
    ESP_ERROR_CHECK(i2s_channel_enable(cs->mic_handle));

    /* Initialize I2C peripheral */
    const i2c_config_t es8311_i2c_cfg = {
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_NUM, &es8311_i2c_cfg), LOG_TAG, "config i2c failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_NUM, I2C_MODE_MASTER, 0, 0, 0), LOG_TAG, "install i2c driver failed");

    /* Initialize es8311 codec */
    es8311_handle_t es_handle = es8311_create(I2C_NUM, ES8311_ADDRRES_0);
    ESP_RETURN_ON_FALSE(es_handle, ESP_FAIL, LOG_TAG, "es8311 create failed");
    const es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = ES8311_MCLK_FREQ_HZ,
        .sample_frequency = ES8311_SAMPLE_RATE};

    ESP_ERROR_CHECK(es8311_init(es_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(es_handle, ES8311_MCLK_FREQ_HZ, ES8311_SAMPLE_RATE), LOG_TAG, "set es8311 sample frequency failed");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(es_handle, ES8311_VOLUME, NULL), LOG_TAG, "set es8311 volume failed");
    ESP_RETURN_ON_ERROR(es8311_microphone_config(es_handle, false), LOG_TAG, "set es8311 microphone failed");
    ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(es_handle, ES8311_MIC_GAIN_6DB), LOG_TAG, "set es8311 microphone gain failed");

    return ESP_OK;
}

static void mic_sync_task(void *args)
{
    void *buf = malloc(2048);
    if (!buf)
    {
        ESP_LOGE(LOG_TAG, "Not enough memory");
        abort();
    }

    size_t actual_len = 0;
    camera_server_t *cs = args;
    while (true)
    {
        if (i2s_channel_read(cs->mic_handle, buf, 2048, &actual_len, 100) == ESP_OK)
        {
            buffer_write(cs->mic_buffer, buf, actual_len);
        }
        else
        {
            vTaskDelay(1);
        }
    }
    vTaskDelete(NULL);
}

static void speaker_sync_task(void *args)
{
    void *buf = malloc(2048);
    if (!buf)
    {
        ESP_LOGE(LOG_TAG, "Not enough memory");
        abort();
    }
    size_t len;
    camera_server_t *cs = args;
    while (true)
    {
        len = buffer_read(cs->speaker_buffer, buf, 2048);
        if (len)
        {
            i2s_channel_write(cs->speaker_handle, buf, len, &len, 100);
        }
        else
        {
            vTaskDelay(1);
        }
    }
    vTaskDelete(NULL);
}

esp_err_t camera_server_init(camera_server_t *camera_server)
{
    camera_server->talking = false;
    // 初始化nvs_flash
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), LOG_TAG, "Failed to erase flash");
        err = nvs_flash_init();
    }

    // 创建一个系统事件任务
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // 注册wifi事件回调函数
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, event_handler_on_wifi_start, camera_server);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, event_handler_on_wifi_disconnected, camera_server);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler_on_ip_got, camera_server);
    esp_event_handler_register(ESP_HTTP_SERVER_EVENT, HTTP_SERVER_EVENT_START, event_handler_on_http_server_start, camera_server);

    ESP_ERROR_CHECK(buffer_init(&camera_server->mic_buffer, 16384));
    ESP_ERROR_CHECK(buffer_init(&camera_server->speaker_buffer, 16384));
    ESP_ERROR_CHECK(camera_init());
    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(sound_init(camera_server));

    xTaskCreate(speaker_sync_task, "speaker_sync", 4096, camera_server, 5, &camera_server->speaker_sync_task);
    xTaskCreate(mic_sync_task, "mic_sync", 4096, camera_server, 5, &camera_server->mic_sync_task);

    return ESP_OK;
}

esp_err_t camera_server_deinit(camera_server_t *camera_server)
{
    camera_server->talking = false;
    vTaskDelete(camera_server->mic_sync_task);
    vTaskDelete(camera_server->speaker_sync_task);
    buffer_deinit(camera_server->mic_buffer);
    buffer_deinit(camera_server->speaker_buffer);
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    return ESP_OK;
}
