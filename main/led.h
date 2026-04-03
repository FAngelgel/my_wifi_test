#pragma once

#include "driver/gpio.h"

// Pin definition
#define LED_GPIO_PIN GPIO_NUM_1

// Output level
enum GPIO_OUTPUT_STATE
{
    PIN_RESET,
    PIN_SET
};

// LED control macros
#define LED(x)          do { x ?                                      \
                             gpio_set_level(LED_GPIO_PIN, PIN_SET) :  \
                             gpio_set_level(LED_GPIO_PIN, PIN_RESET); \
                        } while(0)

#define LED_TOGGLE()    do { gpio_set_level(LED_GPIO_PIN, !gpio_get_level(LED_GPIO_PIN)); } while(0)

void led_init(void);
void led_start_version_blink(void);
