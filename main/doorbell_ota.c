#include "doorbell_ota.h"
#include "doorbell_mqtt.h"
#include "doorbell_config.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

#define TAG "doorbell_ota"

static const char *firmware_uri = "http://192.168.1.101/firmware.bin";

esp_err_t doorbell_ota_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

static void doorbell_ota_process(void *arg)
{
    esp_http_client_config_t config = {
        .url = firmware_uri,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = doorbell_ota_http_event_handler,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    ESP_LOGI(TAG, "Attempting to download update from %s", config.url);
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "OTA Succeed, Rebooting...");
        esp_restart();
    }
    else
    {
        ESP_LOGE(TAG, "Firmware upgrade failed");
    }
}
void doorbell_ota_init()
{
    doorbell_mqtt_callback_t callback = {
        .callback = doorbell_ota_process,
        .arg = NULL,
        .cmd = "ota",
    };
    doorbell_mqtt_register_callback(&callback);
}

void doorbell_ota_confirm()
{
    esp_ota_img_states_t state;
    ESP_ERROR_CHECK(esp_ota_get_state_partition(esp_ota_get_running_partition(), &state));
    if (state != ESP_OTA_IMG_VALID)
    {
        ESP_LOGI("main", "OTA OK");
        ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    }
}
