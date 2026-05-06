#include "MyEventDemo.h"
#include <stdio.h>
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// -------- 1) Declare & define event base --------
ESP_EVENT_DEFINE_BASE(MY_EVENT_BASE);

// -------- 2) Define event IDs --------
enum {
    MY_EVENT_START = 1,
    MY_EVENT_DATA,
    MY_EVENT_STOP
};

static const char *TAG = "MY_EVENT";

// -------- 3) Event handler --------
static void my_event_handler(void *arg,
                             esp_event_base_t base,
                             int32_t id,
                             void *data)
{
    if (base != MY_EVENT_BASE) return;

    switch (id)
    {
        case MY_EVENT_START:
            ESP_LOGI(TAG, "START event received");
            break;

        case MY_EVENT_DATA:
        {
            int value = *(int *)data;
            ESP_LOGI(TAG, "DATA event received: %d", value);
            break;
        }

        case MY_EVENT_STOP:
            ESP_LOGI(TAG, "STOP event received");
            break;

        default:
            ESP_LOGW(TAG, "Unknown event id: %ld", id);
            break;
    }
}

// -------- 4) Demo runner --------
void MyEventDemo_Run(void)
{
    // Create default event loop (only once in your app!)
    //esp_event_loop_create_default();

    // Register handler
    esp_event_handler_instance_t instance;
    esp_event_handler_instance_register(
        MY_EVENT_BASE,
        ESP_EVENT_ANY_ID,
        my_event_handler,
        NULL,
        &instance
    );

    ESP_LOGI(TAG, "Posting START event...");
    esp_event_post(MY_EVENT_BASE, MY_EVENT_START, NULL, 0, portMAX_DELAY);

    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Posting DATA event...");
    int value = 123;
    esp_event_post(MY_EVENT_BASE, MY_EVENT_DATA, &value, sizeof(value), portMAX_DELAY);

    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Posting STOP event...");
    esp_event_post(MY_EVENT_BASE, MY_EVENT_STOP, NULL, 0, portMAX_DELAY);
}