#if !defined(__DOORBELL_BUTTON_H__)
#define __DOORBELL_BUTTON_H__

#include "iot_button.h"
#define BUTTON_COUNT 1

typedef enum
{
    BUTTON_SW2 = 0
} doorbell_button_id;

typedef struct
{
    void (*callback)(void *);
    void *arg;
} doorbell_button_func_t;

void doorbell_button_init();

void doorbell_button_register_callback(doorbell_button_id id, button_event_t action, doorbell_button_func_t *button_cb);

#endif // __DOORBELL_BUTTON_H__
