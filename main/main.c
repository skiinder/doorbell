#include "camera_server.h"
#include "esp_log.h"

static camera_server_t cs;

void app_main()
{
    ESP_LOGI("main", "Booting...");
    camera_server_init(&cs);
}