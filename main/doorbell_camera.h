#if !defined(__DOORBELL_CAMERA_H__)
#define __DOORBELL_CAMERA_H__

#include "esp_camera.h"

void doorbell_camera_init();

esp_err_t doorbell_camera_getJpgFrame(uint8_t **data, size_t *size);

void doorbell_camera_freeJpgFrame(uint8_t *data);

void doorbell_camera_deinit();

#endif // __DOORBELL_CAMERA_H__
