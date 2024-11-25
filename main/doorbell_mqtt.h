#if !defined(__DOORBELL_MQTT_H__)
#define __DOORBELL_MQTT_H__

#include "mqtt_client.h"

typedef struct
{
    char cmd[16];
    void (*callback)(void *);
    void *arg;
} doorbell_mqtt_callback_t;

void doorbell_mqtt_init(void);

void doorbell_mqtt_register_callback(doorbell_mqtt_callback_t *callback);

#endif // __DOORBELL_MQTT_H__
