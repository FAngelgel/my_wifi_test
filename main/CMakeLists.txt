idf_component_register(
    SRCS "wifi.cc" "fourier_analysis.cc" "ota_update.cc" "led.cc" "main.cc"
    INCLUDE_DIRS "."
    REQUIRES 
        nvs_flash 
        esp_event 
        esp_netif  
        esp_http_client 
        esp_https_ota
        esp-tls
        mbedtls
        device_drivers
        esp-wifi-connect-main           
)
