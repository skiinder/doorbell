#if !defined(__DOORBELL_QUEUE_H__)
#define __DOORBELL_QUEUE_H__

#include "freertos/FreeRTOS.h"

#define QUEUE_ITEM_SIZE sizeof(queue_item)

typedef struct
{
    void *data;
    size_t size;
} queue_item;

typedef queue_item *queue_item_t;

#endif // __DOORBELL_QUEUE_H__
