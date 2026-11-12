#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"

#include "config.h"
#include "net.h"
#include "protocol.h"
#include "secrets.h"

static const char *TAG = "net";

// WPA2 exige senha de 8 a 63 caracteres (sizeof conta o '\0' final)
_Static_assert(sizeof(WIFI_SENHA) >= 9 && sizeof(WIFI_SENHA) <= 64,
               "WIFI_SENHA precisa ter de 8 a 63 caracteres");

static void evento_wifi(void *arg, esp_event_base_t base, int32_t id, void *dados)
{
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = (wifi_event_ap_staconnected_t *)dados;
        ESP_LOGI(TAG, "cliente conectado (aid=%d)", e->aid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = (wifi_event_ap_stadisconnected_t *)dados;
        ESP_LOGW(TAG, "cliente desconectado (aid=%d)", e->aid);
    }
}

static void wifi_ap_init(void)
{
    // O driver Wi-Fi guarda dados de calibracao na NVS (memoria nao volatil)
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();   // cria a interface de rede do AP (IP 192.168.4.1)

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &evento_wifi, NULL));

    wifi_config_t ap = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CANAL,
            .password = WIFI_SENHA,
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP no ar: SSID=%s canal=%d IP=192.168.4.1", WIFI_SSID, WIFI_CANAL);
}

static void udp_rx_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() falhou");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(UDP_PORTA),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "bind() falhou na porta %d", UDP_PORTA);
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    // Sem pacote por 1 s, o recvfrom() retorna (-1) em vez de travar para sempre
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "UDP escutando na porta %d", UDP_PORTA);

    uint8_t buf[32];
    cmd_pacote_t ultimo = {0};
    uint32_t rx_janela = 0;
    TickType_t t_janela = xTaskGetTickCount();

    while (true) {
        int n = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);

        if (n == (int)sizeof(cmd_pacote_t)) {
            cmd_pacote_t p;
            memcpy(&p, buf, sizeof(p));
            if (p.magic == MAGIC_CMD) {
                ultimo = p;
                rx_janela++;
            }
        }

        if ((xTaskGetTickCount() - t_janela) >= pdMS_TO_TICKS(1000)) {
            if (rx_janela > 0) {
                ESP_LOGI(TAG, "rx %lu/s | seq=%u acel=%u dir=%d freio=%u",
                         (unsigned long)rx_janela, (unsigned)ultimo.seq,
                         (unsigned)ultimo.throttle, (int)ultimo.steer,
                         (unsigned)ultimo.brake);
            } else {
                ESP_LOGW(TAG, "rx 0/s (sem comandos)");
            }
            rx_janela = 0;
            t_janela = xTaskGetTickCount();
        }
    }
}

void net_init(void)
{
    wifi_ap_init();
    xTaskCreate(udp_rx_task, "udp_rx", 4096, NULL, 5, NULL);
}