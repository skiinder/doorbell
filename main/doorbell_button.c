#include "doorbell_button.h"
#include "doorbell_config.h"
#include <string.h>

static button_handle_t buttons[BUTTON_COUNT];
// 每个按键提供 单击、双击、长按3种功能
static doorbell_button_func_t button_funcs[3 * BUTTON_COUNT];
static int button_funcs_count = 0;

void doorbell_button_callback(void *button_handle, void *usr_data)
{
    doorbell_button_func_t *button_func = (doorbell_button_func_t *)usr_data;
    button_func->callback(button_func->arg);
}

void doorbell_button_init()
{
    button_config_t sw2_config = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,
        .short_press_time = 50,
        .gpio_button_config = {
            .gpio_num = SW2,
            .active_level = 1,
            .enable_power_save = true,
        },
    };
    buttons[BUTTON_SW2] = iot_button_create(&sw2_config);
    assert(buttons[BUTTON_SW2]);
    button_funcs_count = 0;
}

void doorbell_button_register_callback(doorbell_button_id id, button_event_t action, doorbell_button_func_t *button_cb)
{
    memcpy(&button_funcs[button_funcs_count], button_cb, sizeof(doorbell_button_func_t));
    iot_button_register_cb(buttons[id], action, doorbell_button_callback, &button_funcs[button_funcs_count]);
    button_funcs_count++;
}
