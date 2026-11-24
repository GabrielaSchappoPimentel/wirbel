#include "esp_idf_version.h"
#include "esp_log.h"

#include "config.h"
#include "control.h"
#include "net.h"

static const char *TAG = "wirbel";

void app_main(void)
{
    ESP_LOGI(TAG, "Wirbel firmware v%s", FW_VERSION);
    ESP_LOGI(TAG, "ESP-IDF %d.%d", ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR);
    control_init();   // antes da rede: a task de rede ja usa o mutex do controle
    net_init();
}