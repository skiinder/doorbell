#include "doorbell_mqtt.h"
#include "esp_log.h"
#include "cJSON.h"

// #define MQTT_URI "ws://broker.emqx.io:8083"
#define MQTT_URI "ws://broker.emqx.io:8083/mqtt"
#define MQTT_PUSH_TOPIC "doorbell/data"
#define MQTT_PULL_TOPIC "doorbell/cmd"
#define TAG "doorbell_mqtt"

#define MAX_CALLBACK_COUNT 16

static esp_mqtt_client_handle_t client;
static doorbell_mqtt_callback_t mqtt_callbacks[MAX_CALLBACK_COUNT];

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void doorbell_mqtt_executor(void *arg)
{
    doorbell_mqtt_callback_t *cb = arg;
    cb->callback(cb->arg);
    vTaskDelete(NULL);
}

static void doorbell_mqtt_data_handler(char *data)
{
    cJSON *root = cJSON_Parse(data);
    if (!root)
    {
        return;
    }
    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd))
    {
        goto FREE_ROOT;
    }
    ESP_LOGI(TAG, "Received cmd: %s", cmd->valuestring);
    for (size_t i = 0; i < MAX_CALLBACK_COUNT; i++)
    {
        if (strlen(mqtt_callbacks[i].cmd) == 0)
        {
            break;
        }
        if (strcmp(cmd->valuestring, mqtt_callbacks[i].cmd) == 0)
        {
            ESP_LOGI(TAG, "Cmd %s callback matched, creating task", cmd->valuestring);
            if (xTaskCreate(doorbell_mqtt_executor, mqtt_callbacks[i].cmd, 4096, mqtt_callbacks + i, 2, NULL) == pdFALSE)
            {
                ESP_LOGE(TAG, "Failed to create task %s", cmd->valuestring);
            }
            // mqtt_callbacks[i].callback(mqtt_callbacks[i].arg);
            break;
        }
    }
FREE_ROOT:
    cJSON_Delete(root);
}

static void doorbell_mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, MQTT_PULL_TOPIC, 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, MQTT_PUSH_TOPIC, "hello", 5, 0, 0);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);

        doorbell_mqtt_data_handler(event->data);

        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void doorbell_mqtt_init(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_URI,
    };
    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, doorbell_mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void doorbell_mqtt_register_callback(doorbell_mqtt_callback_t *callback)
{
    size_t i;
    for (i = 0; i < MAX_CALLBACK_COUNT; i++)
    {
        if (strlen(mqtt_callbacks[i].cmd) == 0)
        {
            break;
        }
        if (strcmp(mqtt_callbacks[i].cmd, callback->cmd) == 0)
        {
            ESP_LOGW(TAG, "callback for %s already registered, overwrited.", callback->cmd);
            break;
        }
    }
    if (i < MAX_CALLBACK_COUNT)
    {
        memcpy(mqtt_callbacks + i, callback, sizeof(doorbell_mqtt_callback_t));
    }
}

void doorbell_mqtt_publish(char *payload)
{
    esp_mqtt_client_publish(client, MQTT_PUSH_TOPIC, payload, strlen(payload), 0, 0);
}
