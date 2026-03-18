idf_component_register(
    SRCS "main.cc"
    INCLUDE_DIRS "."
    REQUIRES 
        nvs_flash 
        esp_event 
        esp_netif  
        esp_http_client 
        esp-tls
        esp-wifi-connect-main           
)
