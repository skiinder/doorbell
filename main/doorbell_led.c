#include "doorbell_led.h"
#include "led_strip.h"
#include "doorbell_config.h"
#include "doorbell_button.h"
#include "doorbell_mqtt.h"

#define LED_COUNT 2

static led_strip_handle_t strip;
static bool led_on = false;

static void doorbell_led_lightswitch(void *arg)
{
    if (led_on)
    {
        doorbell_led_off();
        return;
    }
    doorbell_led_on();
}

void doorbell_led_init()
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = SW2812_PIN,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format.format = {
            .r_pos = 1,
            .g_pos = 0,
            .b_pos = 2,
            .num_components = 3,
        },
        .flags.invert_out = false,
    };

    led_strip_spi_config_t spi_config = {
        .clk_src = SPI_CLK_SRC_DEFAULT,
        .spi_bus = SPI2_HOST,
        .flags = {
            .with_dma = true,
        }};

    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &strip));
    assert(strip);
    doorbell_led_off();

    // 注册按键回调事件
    doorbell_button_func_t func = {
        .callback = doorbell_led_lightswitch,
        .arg = NULL,
    };
    doorbell_button_register_callback(BUTTON_SW2, BUTTON_LONG_PRESS_START, &func);

    // 注册mqtt回调事件
    doorbell_mqtt_callback_t func2 = {
        .callback = doorbell_led_lightswitch,
        .arg = NULL,
        .cmd = "lightswitch",
    };
    doorbell_mqtt_register_callback(&func2);
}

void doorbell_led_on()
{
    led_on = true;

    led_strip_set_pixel(strip, 0, 255, 255, 255);
    led_strip_set_pixel(strip, 1, 255, 255, 255);
    led_strip_refresh(strip);
}

void doorbell_led_off()
{
    led_on = false;
    led_strip_clear(strip);
}
