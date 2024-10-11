#if !defined(__BUFFER_H__)
#define __BUFFER_H__

#include "esp_types.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef void *buffer_handle_t;

esp_err_t buffer_init(buffer_handle_t *buffer_handle, size_t size);
size_t buffer_read(buffer_handle_t buffer_handle, void *dest, size_t len);
size_t buffer_read_from_isr(buffer_handle_t buffer_handle, void *dest, size_t len);
void buffer_write(buffer_handle_t buffer_handle, void *buf, size_t len);
void buffer_write_from_isr(buffer_handle_t buffer_handle, void *buf, size_t len);
esp_err_t buffer_deinit(buffer_handle_t buffer_handle);

#endif // __BUFFER_H__
