#include "doorbell_httpd.h"
#include "esp_log.h"
#include "doorbell_camera.h"
#include "esp_check.h"

#define TAG "DOORBELL_HTTPD"

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static uint32_t httpd_status; // 记录httpd状态 位0、1记录是否启动 位2、3、4、5记录uri_handler注册状态

extern const uint8_t html_start[] asm("_binary_home_html_start");
extern const uint8_t html_end[] asm("_binary_home_html_end");

#define HTTPD_SERVER_START 0x1
#define STREAM_SERVER_START 0x2
#define ROOT_URI_REGISTER 0x4
#define JPEG_URI_REGISTER 0x8
#define SWITCH_URI_REGISTER 0x10
#define WS_URI_REGISTER 0x20

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)html_start, html_end - html_start);
}

static esp_err_t jpeg_handler(httpd_req_t *req)
{
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len;
    uint8_t *_jpg_buf;
    char *part_buf[64];

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
        res = doorbell_camera_getJpgFrame(&_jpg_buf, &_jpg_buf_len);
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
        if (res == ESP_OK)
        {
            doorbell_camera_freeJpgFrame(_jpg_buf);
        }
        if (res != ESP_OK)
        {
            break;
        }
    }

    return res;
}

static void ws_sync_task(void *arg)
{
    httpd_ws_frame_t ws_pkt;
    doorbell_httpd_handle_t handle = arg;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    ws_pkt.payload = malloc(8192);
    assert(ws_pkt.payload);
    while (handle->talking)
    {
        // 将mic缓存中的数据发送到浏览器
        ws_pkt.len = doorbell_buffer_read(handle->mic_buffer, ws_pkt.payload, 8192);
        if (ws_pkt.len)
        {
            httpd_ws_send_frame_async(handle->http_server, handle->ws_fd, &ws_pkt);
        }
        else
        {
            // 喂看门狗
            vTaskDelay(1);
        }
    }
    vTaskDelete(NULL);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    doorbell_httpd_handle_t handle = req->user_ctx;
    esp_err_t ret = ESP_OK;
    // 在接收到浏览器的请求时，先构建websocket连接
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        handle->talking = true;
        handle->ws_fd = httpd_req_to_sockfd(req);
        xTaskCreate(ws_sync_task, "ws_task", 4096, handle, 1, &handle->ws_sync_task);
        return ESP_OK;
    }

    // websocket数据的帧格式
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    // websocket数据的帧格式是文本格式
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    /* Set max_len = 0 to get the frame len */
    // 将最后一个参数max_len设置为0，用来获取浏览器通过websocket发来的帧格式的长度
    ESP_RETURN_ON_ERROR(httpd_ws_recv_frame(req, &ws_pkt, 0), TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
    if (ws_pkt.len)
    {
        ws_pkt.payload = malloc(ws_pkt.len);
        ESP_RETURN_ON_FALSE(ws_pkt.payload, ESP_ERR_NO_MEM, TAG, "Failed to calloc memory for buf");
        // 获取浏览器通过websocket发送过来的帧格式
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);

        // 正确获取数据后，将数据发送到缓存
        if (ret == ESP_OK && handle->talking)
        {
            doorbell_buffer_write(handle->speaker_buffer, ws_pkt.payload, ws_pkt.len);
        }
        free(ws_pkt.payload);
    }
    return ret;
}

static esp_err_t switch_handler(httpd_req_t *req)
{
    doorbell_httpd_handle_t handle = req->user_ctx;
    assert(handle);
    handle->talking = !handle->talking;
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t doorbell_httpd_init(doorbell_httpd_handle_t *doorbell_httpd_handle,
                              doorbell_buffer_handle_t mic_buffer,
                              doorbell_buffer_handle_t speaker_buffer)
{
    doorbell_httpd_obj *handle = malloc(sizeof(doorbell_httpd_obj));
    if (!handle)
    {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return ESP_ERR_NO_MEM;
    }
    handle->talking = false;
    handle->ws_fd = -1;
    handle->mic_buffer = mic_buffer;
    handle->speaker_buffer = speaker_buffer;

    *doorbell_httpd_handle = handle;
    httpd_status = 0;
    return ESP_OK;
}

void doorbell_httpd_start(doorbell_httpd_handle_t doorbell_httpd_handle)
{
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port = 81;
    stream_config.ctrl_port = 32769;
    ESP_ERROR_CHECK(httpd_start(&doorbell_httpd_handle->http_server, &http_config));
    httpd_status |= HTTPD_SERVER_START;
    ESP_ERROR_CHECK(httpd_start(&doorbell_httpd_handle->stream_server, &stream_config));
    httpd_status |= STREAM_SERVER_START;

    ESP_LOGI(TAG, "Registering uri handlers...");
    httpd_uri_t root_uri = {
        .uri = "/index",
        .method = HTTP_GET,
        .handler = root_handler,
        .is_websocket = true,
        .user_ctx = doorbell_httpd_handle};
    httpd_uri_t jpeg_uri = {
        .uri = "/jpeg",
        .method = HTTP_GET,
        .is_websocket = true,
        .handler = jpeg_handler,
        .user_ctx = doorbell_httpd_handle};
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
        .user_ctx = doorbell_httpd_handle};
    httpd_uri_t switch_uri = {
        .uri = "/switch",
        .method = HTTP_GET,
        .is_websocket = true,
        .handler = switch_handler,
        .user_ctx = doorbell_httpd_handle};
    ESP_ERROR_CHECK(httpd_register_uri_handler(doorbell_httpd_handle->http_server, &root_uri));
    httpd_status |= ROOT_URI_REGISTER;
    ESP_ERROR_CHECK(httpd_register_uri_handler(doorbell_httpd_handle->stream_server, &jpeg_uri));
    httpd_status |= JPEG_URI_REGISTER;
    ESP_ERROR_CHECK(httpd_register_uri_handler(doorbell_httpd_handle->http_server, &ws_uri));
    httpd_status |= WS_URI_REGISTER;
    ESP_ERROR_CHECK(httpd_register_uri_handler(doorbell_httpd_handle->http_server, &switch_uri));
    httpd_status |= SWITCH_URI_REGISTER;
}

void doorbell_httpd_stop(doorbell_httpd_handle_t doorbell_httpd_handle)
{
    if (httpd_status & SWITCH_URI_REGISTER)
    {
        ESP_ERROR_CHECK(httpd_unregister_uri_handler(doorbell_httpd_handle->http_server, "/switch", HTTP_GET));
        httpd_status &= ~SWITCH_URI_REGISTER;
    }
    if (httpd_status & WS_URI_REGISTER)
    {
        ESP_ERROR_CHECK(httpd_unregister_uri_handler(doorbell_httpd_handle->http_server, "/ws", HTTP_GET));
        httpd_status &= ~WS_URI_REGISTER;
    }
    if (httpd_status & JPEG_URI_REGISTER)
    {
        ESP_ERROR_CHECK(httpd_unregister_uri_handler(doorbell_httpd_handle->stream_server, "/jpeg", HTTP_GET));
        httpd_status &= ~JPEG_URI_REGISTER;
    }
    if (httpd_status & ROOT_URI_REGISTER)
    {
        ESP_ERROR_CHECK(httpd_unregister_uri_handler(doorbell_httpd_handle->http_server, "/", HTTP_GET));
        httpd_status &= ~ROOT_URI_REGISTER;
    }
    if (httpd_status & HTTPD_SERVER_START)
    {
        ESP_ERROR_CHECK(httpd_stop(doorbell_httpd_handle->http_server));
        httpd_status &= ~HTTPD_SERVER_START;
    }
    if (httpd_status & STREAM_SERVER_START)
    {
        ESP_ERROR_CHECK(httpd_stop(doorbell_httpd_handle->stream_server));
        httpd_status &= ~STREAM_SERVER_START;
    }
}

void doorbell_httpd_deinit(doorbell_httpd_handle_t doorbell_httpd_handle)
{
    free(doorbell_httpd_handle);
}
