#include "doorbell_wsclient.h"
#include "esp_log.h"

#define NO_DATA_TIMEOUT_SEC 5

static const char *TAG = "websocket";

static TimerHandle_t shutdown_signal_timer;
static SemaphoreHandle_t shutdown_sema;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void shutdown_signaler(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "No data received for %d seconds, signaling shutdown", NO_DATA_TIMEOUT_SEC);
    xSemaphoreGive(shutdown_sema);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_BEGIN");
        break;
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        log_error_if_nonzero("HTTP status code",  data->error_handle.esp_ws_handshake_status_code);
        if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  data->error_handle.esp_transport_sock_errno);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
        if (data->op_code == 0x2) { // Opcode 0x2 indicates binary data
            ESP_LOG_BUFFER_HEX("Received binary data", data->data_ptr, data->data_len);
        } else if (data->op_code == 0x08 && data->data_len == 2) {
            ESP_LOGW(TAG, "Received closed message with code=%d", 256 * data->data_ptr[0] + data->data_ptr[1]);
        } else {
            ESP_LOGW(TAG, "Received=%.*s\n\n", data->data_len, (char *)data->data_ptr);
        }

        // If received data contains json structure it succeed to parse
        cJSON *root = cJSON_Parse(data->data_ptr);
        if (root) {
            for (int i = 0 ; i < cJSON_GetArraySize(root) ; i++) {
                cJSON *elem = cJSON_GetArrayItem(root, i);
                cJSON *id = cJSON_GetObjectItem(elem, "id");
                cJSON *name = cJSON_GetObjectItem(elem, "name");
                ESP_LOGW(TAG, "Json={'id': '%s', 'name': '%s'}", id->valuestring, name->valuestring);
            }
            cJSON_Delete(root);
        }

        ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);

        xTimerReset(shutdown_signal_timer, portMAX_DELAY);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        log_error_if_nonzero("HTTP status code",  data->error_handle.esp_ws_handshake_status_code);
        if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  data->error_handle.esp_transport_sock_errno);
        }
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_FINISH");
        break;
    }
}

static void websocket_app_start(void)
{
    esp_websocket_client_config_t websocket_cfg = {};

    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
    shutdown_sema = xSemaphoreCreateBinary();

    websocket_cfg.uri = CONFIG_WEBSOCKET_URI;

    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

    esp_websocket_client_start(client);
    xTimerStart(shutdown_signal_timer, portMAX_DELAY);
    char data[32];
    int i = 0;
    while (i < 5) {
        if (esp_websocket_client_is_connected(client)) {
            int len = sprintf(data, "hello %04d", i++);
            ESP_LOGI(TAG, "Sending %s", data);
            esp_websocket_client_send_text(client, data, len, portMAX_DELAY);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    // Sending text data
    ESP_LOGI(TAG, "Sending fragmented text message");
    memset(data, 'a', sizeof(data));
    esp_websocket_client_send_text_partial(client, data, sizeof(data), portMAX_DELAY);
    memset(data, 'b', sizeof(data));
    esp_websocket_client_send_cont_msg(client, data, sizeof(data), portMAX_DELAY);
    esp_websocket_client_send_fin(client, portMAX_DELAY);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Sending binary data
    ESP_LOGI(TAG, "Sending fragmented binary message");
    char binary_data[5];
    memset(binary_data, 0, sizeof(binary_data));
    esp_websocket_client_send_bin_partial(client, binary_data, sizeof(binary_data), portMAX_DELAY);
    memset(binary_data, 1, sizeof(binary_data));
    esp_websocket_client_send_cont_msg(client, binary_data, sizeof(binary_data), portMAX_DELAY);
    esp_websocket_client_send_fin(client, portMAX_DELAY);
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Sending text data longer than ws buffer (default 1024)
    ESP_LOGI(TAG, "Sending text longer than ws buffer (default 1024)");
    const int size = 2000;
    char *long_data = malloc(size);
    memset(long_data, 'a', size);
    esp_websocket_client_send_text(client, long_data, size, portMAX_DELAY);

    xSemaphoreTake(shutdown_sema, portMAX_DELAY);
    esp_websocket_client_close(client, portMAX_DELAY);
    ESP_LOGI(TAG, "Websocket Stopped");
    esp_websocket_client_destroy(client);
}


void doorbell_wsclient_init()
{
}