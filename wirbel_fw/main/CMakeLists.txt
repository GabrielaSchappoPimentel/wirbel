idf_component_register(SRCS "main.c" "net.c" "control.c" "act.c" "battery.c" "console.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi esp_netif esp_event nvs_flash lwip
                                esp_driver_ledc esp_driver_gpio esp_driver_uart esp_adc)
