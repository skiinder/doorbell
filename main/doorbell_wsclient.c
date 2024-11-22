#include "doorbell_wsclient.h"
#include "esp_log.h"
#include "doorbell_camera.h"

#define CAM_URI "ws://example.com:80"
#define SND_URI "ws://example.com:81"

static const char *TAG = "websocket";

typedef struct
{
    esp_websocket_client_handle_t cam_handle;
    esp_websocket_client_handle_t sound_handle;
    RingbufferType_t mic_buffer;
    RingbufferType_t speaker_buffer;
    bool talking;
} doorbell_wsclient_obj;

doorbell_wsclient_obj *doorbell_wsclient_handle = NULL;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id)
    {
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_BEGIN");
        break;
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        log_error_if_nonzero("HTTP status code", data->error_handle.esp_ws_handshake_status_code);
        if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", data->error_handle.esp_transport_sock_errno);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
        if (data->op_code == 0x2)
        {
            // 收到二进制数据
            if (data->client == doorbell_wsclient_handle->sound_handle)
            {
                xRingbufferSend(doorbell_wsclient_handle->speaker_buffer, data->data_ptr, data->data_len, portMAX_DELAY);
            }
        }
        else if (data->op_code == 0x08 && data->data_len == 2)
        {
            ESP_LOGW(TAG, "Received closed message with code=%d", 256 * data->data_ptr[0] + data->data_ptr[1]);
        }
        else
        {
            ESP_LOGW(TAG, "Received=%.*s\n\n", data->data_len, (char *)data->data_ptr);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        log_error_if_nonzero("HTTP status code", data->error_handle.esp_ws_handshake_status_code);
        if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", data->error_handle.esp_transport_sock_errno);
        }
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_FINISH");
        break;
    }
}

static void doorbell_wsclient_cam_task(void *arg)
{
    doorbell_wsclient_obj *handle = arg;
    uint8_t *buf = NULL;
    size_t buf_len = 0;
    while (true)
    {
        if (!handle->talking)
        {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_ERROR_CHECK(doorbell_camera_getJpgFrame(&buf, &buf_len));
        ESP_ERROR_CHECK(esp_websocket_client_send_bin(handle->cam_handle, buf, buf_len, portMAX_DELAY));
        doorbell_camera_freeJpgFrame(buf);
    }
    vTaskDelete(NULL);
}

static void doorbell_wsclient_sound_task(void *arg)
{
    doorbell_wsclient_obj *handle = arg;
    void *buf;
    size_t buf_len;
    while (true)
    {
        if (!handle->talking)
        {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }
        buf = xRingbufferReceive(handle->mic_buffer, &buf_len, portMAX_DELAY);
        if (buf)
        {
            ESP_ERROR_CHECK(esp_websocket_client_send_bin(handle->sound_handle, buf, buf_len, portMAX_DELAY));
            vRingbufferReturnItem(handle->mic_buffer, buf);
        }
    }
    vTaskDelete(NULL);
}

void doorbell_wsclient_init(RingbufHandle_t mic_buffer, RingbufHandle_t speaker_buffer)
{
    doorbell_wsclient_handle = malloc(sizeof(doorbell_wsclient_obj));
    assert(doorbell_wsclient_handle);

    doorbell_wsclient_handle->mic_buffer = mic_buffer;
    doorbell_wsclient_handle->speaker_buffer = speaker_buffer;
    doorbell_wsclient_handle->talking = false;

    esp_websocket_client_config_t cfg = {
        .uri = CAM_URI,
    };
    doorbell_wsclient_handle->cam_handle = esp_websocket_client_init(&cfg);
    cfg.uri = SND_URI;
    doorbell_wsclient_handle->sound_handle = esp_websocket_client_init(&cfg);

    esp_websocket_register_events(doorbell_wsclient_handle->cam_handle, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)doorbell_wsclient_handle);
    esp_websocket_register_events(doorbell_wsclient_handle->sound_handle, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)doorbell_wsclient_handle);
}

void doorbell_wsclient_start()
{
    esp_websocket_client_start(doorbell_wsclient_handle->cam_handle);
    esp_websocket_client_start(doorbell_wsclient_handle->sound_handle);
    xTaskCreate(doorbell_wsclient_cam_task, "doorbell_wsclient_cam_task", 4096, NULL, 5, doorbell_wsclient_handle);
    xTaskCreate(doorbell_wsclient_sound_task, "doorbell_wsclient_sound_task", 4096, NULL, 5, doorbell_wsclient_handle);
}

void doorbell_wsclient_switch_talking()
{
    doorbell_wsclient_handle->talking = !doorbell_wsclient_handle->talking;
}
