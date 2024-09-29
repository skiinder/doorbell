#include "camera_pins.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "esp_camera.h"

#define LOG_TAG "camera_server"

#define WIFI_CONNECT_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static EventGroupHandle_t event_group_handle;

static void event_handler(void *event_handler_arg,
                          esp_event_base_t event_base,
                          int32_t event_id,
                          void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(LOG_TAG, "WiFi started, try to connect...");
        ESP_ERROR_CHECK(esp_wifi_connect());
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGE(LOG_TAG, "WiFi disconnected...");
        xEventGroupSetBits(event_group_handle, WIFI_FAIL_BIT);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(LOG_TAG, "Got ip address....");
        xEventGroupSetBits(event_group_handle, WIFI_CONNECT_BIT);
    }
}

static void camera_server_init()
{
    event_group_handle = xEventGroupCreate();
    // 初始化nvs_flash，用于存储wifi配置信息
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    // 初始化TCP/IP栈
    ESP_ERROR_CHECK(esp_netif_init());

    // 创建一个系统事件任务
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 创建wifi网卡
    esp_netif_t *netif_sta = esp_netif_create_default_wifi_sta();
    assert(netif_sta);

    // 初始化wifi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册wifi事件回调函数
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, event_handler, NULL, NULL);
    esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL, NULL);

    wifi_config_t config = {
        .sta = {
            .ssid = CONFIG_EXAMPLE_WIFI_SSID,
            .password = CONFIG_EXAMPLE_WIFI_PASSWORD,
            .scan_method = CONFIG_EXAMPLE_WIFI_SCAN_METHOD_ALL_CHANNEL ? WIFI_ALL_CHANNEL_SCAN : WIFI_FAST_SCAN}

    };
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 初始化摄像头

    // 等待wifi连接成功
    xEventGroupWaitBits(event_group_handle,
                        WIFI_CONNECT_BIT | WIFI_FAIL_BIT,
                        pdFALSE, pdFALSE, portMAX_DELAY);
}
