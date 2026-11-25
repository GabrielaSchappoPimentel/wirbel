#include "esp_idf_version.h"
#include "esp_log.h"

#include "config.h"
#include "console.h"
#include "control.h"
#include "net.h"

static const char *TAG = "wirbel";

void app_main(void)
{
    ESP_LOGI(TAG, "Wirbel firmware v%s", FW_VERSION);
    ESP_LOGI(TAG, "ESP-IDF %d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR);
    control_init();   // primeiro: atuadores em posicao segura e mutex criado
    console_init();
    net_init();
}
