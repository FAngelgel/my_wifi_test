#include "mqtt_app.h"

#include <atomic>
#include <cstdio>
#include <string>

extern "C"
{
#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <cJSON.h>
#include <mqtt_client.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
}

namespace
{

constexpr const char *TAG = "mqtt_app";

constexpr const char *kBrokerUri = "mqtt://broker.emqx.io";
constexpr const char *kCommandTopic = "yangyang/eeg/command";
constexpr const char *kDataTopic = "yangyang/eeg/data";

std::atomic<bool> s_started{false};
std::atomic<bool> s_connected{false};
std::atomic<bool> s_publish_enabled{false};

esp_mqtt_client_handle_t s_client = nullptr;
std::string s_client_id;

std::string trim_ascii(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t'))
    {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && (s[start] == '\r' || s[start] == '\n' || s[start] == ' ' || s[start] == '\t'))
    {
        ++start;
    }
    if (start > 0)
    {
        s.erase(0, start);
    }
    return s;
}

void publish_task(void * /*arg*/)
{
    int value = 0;
    while (true)
    {
        if (s_connected.load() && s_publish_enabled.load() && s_client != nullptr)
        {
            char payload[64];
            std::snprintf(payload, sizeof(payload), "{\"value\":%d}", value++);
            const int msg_id = esp_mqtt_client_publish(s_client, kDataTopic, payload, 0, 0, 0);
            ESP_LOGI(TAG, "Published %s (msg_id=%d)", payload, msg_id);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void mqtt_event_handler(void * /*handler_args*/, esp_event_base_t /*base*/, int32_t event_id, void *event_data)
{
    auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
    if (!event)
    {
        return;
    }

    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED:
        s_connected.store(true);
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_subscribe(event->client, kCommandTopic, 0);
        break;
    case MQTT_EVENT_DATA: {
        const std::string topic(event->topic, event->topic + event->topic_len);
        const std::string data(event->data, event->data + event->data_len);
        const std::string msg = trim_ascii(data);

        if (topic == kCommandTopic)
        {
            const auto handle_cmd = [](const std::string &cmd) {
                if (cmd == "START")
                {
                    s_publish_enabled.store(true);
                    ESP_LOGI(TAG, "Command START -> publishing enabled");
                    return true;
                }
                if (cmd == "STOP")
                {
                    s_publish_enabled.store(false);
                    ESP_LOGI(TAG, "Command STOP -> publishing disabled");
                    return true;
                }
                return false;
            };

            if (handle_cmd(msg))
            {
                break;
            }

            if (!msg.empty() && msg.front() == '{')
            {
                cJSON *root = cJSON_Parse(msg.c_str());
                if (!root)
                {
                    ESP_LOGW(TAG, "Invalid JSON command");
                    break;
                }

                const cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
                if (!cJSON_IsString(cmd) || cmd->valuestring == nullptr)
                {
                    cmd = cJSON_GetObjectItem(root, "command");
                }

                if (cJSON_IsString(cmd) && cmd->valuestring != nullptr)
                {
                    const std::string cmd_str = trim_ascii(cmd->valuestring);
                    if (!handle_cmd(cmd_str))
                    {
                        ESP_LOGW(TAG, "Unknown JSON cmd: %s", cmd_str.c_str());
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "JSON missing string field 'cmd'/'command'");
                }

                cJSON_Delete(root);
                break;
            }

            ESP_LOGW(TAG, "Unknown command: %s", msg.c_str());
        }
        break;
    }
    default:
        if (event_id == MQTT_EVENT_DISCONNECTED)
        {
            s_connected.store(false);
            ESP_LOGW(TAG, "MQTT disconnected");
        }
        break;
    }
}

void mqtt_start_if_needed()
{
    if (s_started.exchange(true))
    {
        return;
    }

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "EEG_detect_%02X%02X%02X", mac[3], mac[4], mac[5]);
    s_client_id = tmp;

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = kBrokerUri;
    cfg.credentials.client_id = s_client_id.c_str();

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client)
    {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return;
    }

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, &mqtt_event_handler, nullptr);
    esp_mqtt_client_start(s_client);

    static constexpr uint32_t kPublishStack = 4096;
    static constexpr UBaseType_t kPublishPrio = 4;
    xTaskCreate(&publish_task, "mqtt_pub", kPublishStack, nullptr, kPublishPrio, nullptr);
}

void on_got_ip(void * /*arg*/, esp_event_base_t event_base, int32_t event_id, void * /*event_data*/)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        mqtt_start_if_needed();
    }
}

} // namespace

void mqtt_app_start()
{
    // Start MQTT when station gets an IP (internet available).
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, nullptr));
}
