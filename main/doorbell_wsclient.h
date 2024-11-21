#if !defined(__DOORBELL_WSCLIENT_H__)
#define __DOORBELL_WSCLIENT_H__

#include "esp_types.h"
#include "esp_websocket_client.h"
#include "freertos/ringbuf.h"

typedef struct
{
    char* url;
} doorbell_wsclient_t;

void doorbell_wsclient_init(RingbufHandle_t mic_buffer, RingbufHandle_t speaker_buffer);

void doorbell_wsclient_send_data(void *data, size_t len);

#endif // __DOORBELL_WSCLIENT_H__
