#include "buffer.h"
#include "string.h"


typedef struct
{
    void *ptr;
    size_t size;
    size_t start;
    size_t len;
    portMUX_TYPE mutex;
} buffer_obj;

esp_err_t buffer_init(buffer_handle_t *buffer_handle, size_t size)
{
    buffer_obj *buffer = malloc(sizeof(buffer_obj));
    if (!buffer)
    {
        goto HANDLE_MALLOC_FAIL;
    }
    buffer->ptr = malloc(size);
    if (!buffer->ptr)
    {
        goto PTR_MOLLOC_FAIL;
    }
    buffer->size = size;
    buffer->start = 0;
    buffer->len = 0;
    *buffer_handle = buffer;
    portMUX_INITIALIZE(&buffer->mutex);

    return ESP_OK;

PTR_MOLLOC_FAIL:
    free(buffer);

HANDLE_MALLOC_FAIL:
    return ESP_ERR_NO_MEM;
}

static inline void buffer_read_internal(buffer_obj *buffer, void *dest, size_t len)
{
    if (buffer->start + len > buffer->size)
    {
        size_t first_len = buffer->size - buffer->start;
        memcpy(dest, buffer->ptr + buffer->start, first_len);
        memcpy(dest + first_len, buffer->ptr, len - first_len);
        buffer->start = len - first_len;
    }
    else
    {
        memcpy(dest, buffer->ptr + buffer->start, len);
        buffer->start += len;
    }
    buffer->len -= len;
}

size_t buffer_read(buffer_handle_t buffer_handle, void *dest, size_t len)
{
    assert(buffer_handle);
    assert(dest);
    buffer_obj *buffer = buffer_handle;
    if (len > buffer->len)
    {
        len = buffer->len;
    }

    if (len == 0)
    {
        return 0;
    }

    portENTER_CRITICAL(&buffer->mutex);
    buffer_read_internal(buffer, dest, len);
    portEXIT_CRITICAL(&buffer->mutex);
    return len;
}

size_t buffer_read_from_isr(buffer_handle_t buffer_handle, void *dest, size_t len)
{
    assert(buffer_handle);
    assert(dest);
    buffer_obj *buffer = buffer_handle;
    if (len > buffer->len)
    {
        len = buffer->len;
    }

    if (len == 0)
    {
        return 0;
    }

    portENTER_CRITICAL_ISR(&buffer->mutex);
    buffer_read_internal(buffer, dest, len);
    portEXIT_CRITICAL_ISR(&buffer->mutex);

    return len;
}

static inline void buffer_write_internal(buffer_obj *buffer, void *buf, size_t len)
{
    if (buffer->len + len > buffer->size)
    {
        // 缓冲区长度不够，出现覆盖
        buffer->start += buffer->len + len - buffer->size;
        if (buffer->start > buffer->size)
        {
            buffer->start -= buffer->size;
        }

        buffer->len = buffer->size - len;
    }

    size_t write_offset = buffer->start + buffer->len;
    if (write_offset > buffer->size)
    {
        write_offset -= buffer->size;
    }
    
    if (write_offset + len > buffer->size)
    {
        size_t first_len = buffer->size - write_offset;
        memcpy(buffer->ptr + write_offset, buf, first_len);
        memcpy(buffer->ptr, buf + first_len, len - first_len);
    }
    else
    {
        memcpy(buffer->ptr + write_offset, buf, len);
    }
    buffer->len += len;
}

void buffer_write(buffer_handle_t buffer_handle, void *buf, size_t len)
{
    assert(buffer_handle);
    assert(buf);
    buffer_obj *buffer = buffer_handle;
    if (len == 0)
    {
        return;
    }

    if (len > buffer->size)
    {
        buf += len - buffer->size;
        len = buffer->size;
    }

    portENTER_CRITICAL(&buffer->mutex);
    buffer_write_internal(buffer, buf, len);
    portEXIT_CRITICAL(&buffer->mutex);
}

void buffer_write_from_isr(buffer_handle_t buffer_handle, void *buf, size_t len)
{
    assert(buffer_handle);
    assert(buf);
    buffer_obj *buffer = buffer_handle;
    if (len == 0)
    {
        return;
    }

    if (len > buffer->size)
    {
        buf += len - buffer->size;
        len = buffer->size;
    }
    portENTER_CRITICAL_ISR(&buffer->mutex);
    buffer_write_internal(buffer, buf, len);
    portEXIT_CRITICAL_ISR(&buffer->mutex);
}

esp_err_t buffer_deinit(buffer_handle_t buffer_handle)
{
    if (!buffer_handle)
    {
        return ESP_FAIL;
    }
    buffer_obj *buffer = buffer_handle;
    free(buffer->ptr);
    free(buffer);
    return ESP_OK;
}
