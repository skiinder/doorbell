#include "doorbell_wifi.h"
#include "doorbell_config.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "doorbell_wifi";
static void (*connected_callback)(void);
static void (*disconnected_callback)(void);

static void doorbell_wifi_event_handler(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WiFi started, trying to connect...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "IP address assigned");
        if (connected_callback)
        {
            connected_callback();
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "WiFi disconnected");
        if (disconnected_callback)
        {
            disconnected_callback();
        }
    }
}

void doorbell_wifi_init(wifi_callback_t connect_callback, wifi_callback_t disconnect_callback)
{
    // 初始化WIFI的配置存储
    ESP_ERROR_CHECK(nvs_flash_init());

    // 创建一个默认事件通知系统
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 注册wifi相关事件
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, doorbell_wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, doorbell_wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, doorbell_wifi_event_handler, NULL));

    // WIFI配置
    wifi_config_t config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .scan_method = WIFI_FAST_SCAN}

    };

    // 初始化TCP/IP栈
    ESP_ERROR_CHECK(esp_netif_init());

    // 创建wifi网卡
    esp_netif_t *netif_sta = esp_netif_create_default_wifi_sta();
    assert(netif_sta);

    // 初始化wifi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    connected_callback = connect_callback;
    disconnected_callback = disconnect_callback;
}

void doorbell_wifi_start(void)
{
    ESP_ERROR_CHECK(esp_wifi_start());
}

void doorbell_wifi_stop(void)
{
    ESP_ERROR_CHECK(esp_wifi_stop());
}

void doorbell_wifi_deinit(void)
{
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_netif_deinit());
    ESP_ERROR_CHECK(nvs_flash_deinit());
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
    ESP_ERROR_CHECK(nvs_flash_deinit());
}
