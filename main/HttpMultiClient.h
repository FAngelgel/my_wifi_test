#ifndef HTTP_MULTI_CLIENT_H
#define HTTP_MULTI_CLIENT_H

#include "esp_err.h"
#include "esp_http_client.h"

class HttpMultiClient
{
public:
    HttpMultiClient();

    // start both requests
    void start();

private:
    // hide implementation details
    struct RequestContext;

    static esp_err_t http_event_handler(esp_http_client_event_t *evt);
};

#endif
