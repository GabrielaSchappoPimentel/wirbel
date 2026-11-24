idf_component_register(SRCS "main.c" "net.c" "control.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_netif esp_event nvs_flash lwip)