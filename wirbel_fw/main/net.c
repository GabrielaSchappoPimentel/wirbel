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
#include "control.h"
#include "net.h"
#include "protocol.h"
#include "secrets.h"
#include "util.h"

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
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

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

static void udp_task(void *arg)
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

    // Espera no maximo 20 ms por pacote: o laco roda ~50x/s mesmo sem comandos
    struct timeval tv = { .tv_sec = 0, .tv_usec = 20000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "UDP escutando na porta %d", UDP_PORTA);

    uint8_t buf[32];
    struct sockaddr_in cliente = {0};
    bool tem_cliente = false;
    uint32_t rx_janela = 0;
    uint32_t rx_por_s = 0;
    uint32_t t_janela = agora_ms();
    uint32_t t_tel = agora_ms();
    uint32_t t_log = agora_ms();

    while (true) {
        struct sockaddr_in origem;
        socklen_t origem_len = sizeof(origem);
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&origem, &origem_len);

        if (n == (int)sizeof(cmd_pacote_t)) {
            cmd_pacote_t p;
            memcpy(&p, buf, sizeof(p));
            if (p.magic == MAGIC_CMD && control_novo_comando(&p)) {
                cliente = origem;   // a telemetria volta para quem mandou
                tem_cliente = true;
                rx_janela++;
            }
        }

        uint32_t agora = agora_ms();

        // ---- Telemetria a 10 Hz ----
        if (tem_cliente && (uint32_t)(agora - t_tel) >= TELEMETRIA_PERIODO_MS) {
            t_tel = agora;
            control_snapshot_t s;
            control_snapshot(&s);
            tel_pacote_t t = {
                .magic    = MAGIC_TEL,
                .seq_eco  = s.seq_eco,
                .estado   = (uint8_t)s.estado,
                .vbat_mv  = s.vbat_mv,
                .throttle = s.throttle,
                .steer    = s.steer,
                .brake    = s.brake,
                .rx_por_s = (uint8_t)(rx_por_s > 255 ? 255 : rx_por_s),
            };
            sendto(sock, &t, sizeof(t), 0, (struct sockaddr *)&cliente, sizeof(cliente));
        }

        // ---- Janela de 1 s: taxa de pacotes ----
        if ((uint32_t)(agora - t_janela) >= 1000) {
            rx_por_s = rx_janela;
            rx_janela = 0;
            t_janela = agora;
        }

        // ---- Log a cada 5 s (menos poluicao durante a calibracao) ----
        if ((uint32_t)(agora - t_log) >= 5000) {
            t_log = agora;
            control_snapshot_t s;
            control_snapshot(&s);
            if (!s.calibrando) {
                ESP_LOGI(TAG, "rx %lu/s | %s | vbat=%u mV acel=%u dir=%d freio=%u drs=%u%%",
                         (unsigned long)rx_por_s, control_nome_estado(s.estado),
                         (unsigned)s.vbat_mv, (unsigned)s.throttle, (int)s.steer,
                         (unsigned)s.brake, (unsigned)s.drs_pct);
            }
        }
    }
}

void net_init(void)
{
    wifi_ap_init();
    xTaskCreate(udp_task, "udp", 4096, NULL, 5, NULL);
}
