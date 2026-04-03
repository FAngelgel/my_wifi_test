#include "led.h"

#include <esp_app_desc.h>

#include <cctype>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

int parse_blink_count(const char *version)
{
    if (version == nullptr || version[0] == '\0')
    {
        return 1;
    }

    const char *p = version;
    if (p[0] == 'v' || p[0] == 'V')
    {
        ++p;
    }

    int value = 0;
    bool any = false;
    while (*p)
    {
        if (std::isdigit(static_cast<unsigned char>(*p)))
        {
            value = value * 10 + (*p - '0');
            any = true;
        }
        else
        {
            break;
        }
        ++p;
    }

    if (!any)
    {
        return 1;
    }
    if (value < 1) value = 1;
    if (value > 9) value = 9;
    return value;
}

void led_blink_task(void * /*arg*/)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    const int count = parse_blink_count(desc ? desc->version : nullptr);

    while (true)
    {
        for (int i = 0; i < count; ++i)
        {
            LED_TOGGLE();
            vTaskDelay(pdMS_TO_TICKS(150));
            LED_TOGGLE();
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

} // namespace

void led_init(void)
{
    gpio_config_t gpio_init_struct = {};

    gpio_init_struct.intr_type = GPIO_INTR_DISABLE;
    gpio_init_struct.mode = GPIO_MODE_INPUT_OUTPUT;
    gpio_init_struct.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_init_struct.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_init_struct.pin_bit_mask = 1ull << LED_GPIO_PIN;
    gpio_config(&gpio_init_struct);

    // Default to off
    LED(1);
}

void led_start_version_blink(void)
{
    static constexpr uint32_t kStack = 2048;
    static constexpr UBaseType_t kPrio = 3;
    xTaskCreate(&led_blink_task, "led_blink", kStack, nullptr, kPrio, nullptr);
}
