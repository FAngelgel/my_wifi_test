#include "HttpMultiClient.h"
#include <cstring>
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "HTTP_CPP";

// -------- context structure --------
struct HttpMultiClient::RequestContext
{
    int type;              // 1 or 2
    char buffer[512];
    int len;
};

// -------- HTTP event handler --------
esp_err_t HttpMultiClient::http_event_handler(esp_http_client_event_t *evt)
{
    auto *ctx = static_cast<RequestContext *>(evt->user_data);
    if (ctx == nullptr)
    {
        return ESP_OK;
    }

    switch (evt->event_id)
    {
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "Client %d connected", ctx->type);
            break;

        case HTTP_EVENT_ON_DATA:

            if (ctx->len + evt->data_len < sizeof(ctx->buffer))
            {
                memcpy(ctx->buffer + ctx->len,
                       evt->data,
                       evt->data_len);

                ctx->len += evt->data_len;
            }

            break;

        case HTTP_EVENT_ON_FINISH:

            ctx->buffer[ctx->len] = '\0';

            ESP_LOGI(TAG, "Client %d finished", ctx->type);
            ESP_LOGI(TAG, "Response:\n%s", ctx->buffer);

            break;

        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "Client %d error", ctx->type);
            break;

        default:
            break;
    }

    return ESP_OK;
}

// -------- class implementation --------

HttpMultiClient::HttpMultiClient()
{
}

void HttpMultiClient::start()
{
    // --- context objects ---
    static RequestContext ctx1 = {1, {0}, 0};
    static RequestContext ctx2 = {2, {0}, 0};

    // -------- client 1 --------
    esp_http_client_config_t config1 = {};
    config1.url = "http://httpbin.org/get";
    config1.event_handler = &HttpMultiClient::http_event_handler;
    config1.user_data = &ctx1;

    esp_http_client_handle_t client1 =
        esp_http_client_init(&config1);

    ESP_LOGI(TAG, "Starting client 1...");
    esp_http_client_perform(client1);

    // -------- client 2 --------
    esp_http_client_config_t config2 = {};
    config2.url = "http://httpbin.org/uuid";
    config2.event_handler = &HttpMultiClient::http_event_handler;
    config2.user_data = &ctx2;

    esp_http_client_handle_t client2 =
        esp_http_client_init(&config2);

    ESP_LOGI(TAG, "Starting client 2...");
    esp_http_client_perform(client2);

    // cleanup
    esp_http_client_cleanup(client1);
    esp_http_client_cleanup(client2);
}
