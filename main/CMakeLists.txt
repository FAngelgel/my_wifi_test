idf_component_register(
    SRCS "wifi.cc" "spi_example.cc" "fourier_analysis.cc" "main.cc"
    INCLUDE_DIRS "."
    REQUIRES 
        nvs_flash 
        esp_event 
        esp_netif  
        esp_http_client 
        esp-tls
        driver
        esp-wifi-connect-main           
)
