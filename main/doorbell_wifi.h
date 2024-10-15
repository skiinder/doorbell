#if !defined(__DOORBELL_WIFI_H__)
#define __DOORBELL_WIFI_H__

#include "esp_wifi.h"

typedef void (*wifi_callback_t)(void);

void doorbell_wifi_init(wifi_callback_t connect_callback, wifi_callback_t disconnect_callback);
void doorbell_wifi_start(void);
void doorbell_wifi_stop(void);
void doorbell_wifi_deinit(void);

#endif // __DOORBELL_WIFI_H__
