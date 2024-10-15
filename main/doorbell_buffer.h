#if !defined(__BUFFER_H__)
#define __BUFFER_H__

#include "esp_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct
{
    void *ptr;
    size_t size;
    size_t start;
    size_t len;
    portMUX_TYPE mutex;
} doorbell_buffer_obj;

typedef doorbell_buffer_obj *doorbell_buffer_handle_t;

esp_err_t doorbell_buffer_init(doorbell_buffer_handle_t *buffer_handle, size_t size);
size_t doorbell_buffer_read(doorbell_buffer_handle_t buffer_handle, void *dest, size_t len);
size_t doorbell_buffer_read_from_isr(doorbell_buffer_handle_t buffer_handle, void *dest, size_t len);
void doorbell_buffer_write(doorbell_buffer_handle_t buffer_handle, void *buf, size_t len);
void doorbell_buffer_write_from_isr(doorbell_buffer_handle_t buffer_handle, void *buf, size_t len);
esp_err_t doorbell_buffer_deinit(doorbell_buffer_handle_t buffer_handle);

#endif // __BUFFER_H__
