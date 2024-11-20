#ifndef CAMERA_PINS_H_
#define CAMERA_PINS_H_

// 摄像头配置
#define CAM_PIN_PWDN -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK 37
#define CAM_PIN_SIOD 0
#define CAM_PIN_SIOC 1

#define CAM_PIN_D7 38
#define CAM_PIN_D6 36
#define CAM_PIN_D5 35
#define CAM_PIN_D4 33
#define CAM_PIN_D3 10
#define CAM_PIN_D2 12
#define CAM_PIN_D1 11
#define CAM_PIN_D0 9
#define CAM_PIN_VSYNC 41
#define CAM_PIN_HREF 39
#define CAM_PIN_PCLK 34

// 共享WiFi的用户名和密码
#define WIFI_SSID "atguigu"
#define WIFI_PASSWORD "aqswdefr"

// 音频配置
#define I2C_NUM 0
#define I2C_SCL_IO 1
#define I2C_SDA_IO 0

/* I2S port and GPIOs */
#define I2S_NUM 0
#define I2S_MCK_IO 3
#define I2S_BCK_IO 2
#define I2S_WS_IO 5
#define I2S_DO_IO 6
#define I2S_DI_IO 4

#define PA_PIN 46

#endif
