#if !defined(__DOORBELL_WSCLIENT_H__)
#define __DOORBELL_WSCLIENT_H__

#include "esp_types.h"
#include "esp_websocket_client.h"
#include "freertos/ringbuf.h"

void doorbell_wsclient_init(RingbufHandle_t mic_buffer, RingbufHandle_t speaker_buffer);

void doorbell_wsclient_start();

void doorbell_wsclient_switch_talking();

#endif // __DOORBELL_WSCLIENT_H__
