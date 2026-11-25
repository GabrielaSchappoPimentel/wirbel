#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "act.h"
#include "console.h"
#include "control.h"

static const char *TAG = "console";

static void ajuda(void)
{
    ESP_LOGI(TAG, "Comandos (digite e aperte Enter):");
    ESP_LOGI(TAG, "  cal          entra no modo calibracao");
    ESP_LOGI(TAG, "  sair         volta ao modo normal");
    ESP_LOGI(TAG, "  d <us>       servo da direcao (ex.: d 1500)");
    ESP_LOGI(TAG, "  f <us>       servo do freio");
    ESP_LOGI(TAG, "  r <us>       servo do DRS");
    ESP_LOGI(TAG, "  ma <pct>     motor A por 2 s (ex.: ma 30)");
    ESP_LOGI(TAG, "  mb <pct>     motor B por 2 s");
    ESP_LOGI(TAG, "  bat          mostra a tensao da bateria");
}

static void processar(const char *l)
{
    int v;
    control_snapshot_t s;
    control_snapshot(&s);

    if (strcmp(l, "?") == 0 || strcmp(l, "ajuda") == 0) {
        ajuda();
    } else if (strcmp(l, "bat") == 0) {
        ESP_LOGI(TAG, "bateria: %u mV", (unsigned)s.vbat_mv);
    } else if (strcmp(l, "cal") == 0) {
        control_calib(true);
        ESP_LOGI(TAG, "calibracao ON. Servos foram para os valores do config.h");
    } else if (strcmp(l, "sair") == 0) {
        control_calib(false);
    } else if (!s.calibrando) {
        ESP_LOGW(TAG, "digite 'cal' primeiro (ou '?' para ajuda)");
    } else if (sscanf(l, "d %d", &v) == 1) {
        control_calib_servo(SERVO_DIRECAO, v);
        ESP_LOGI(TAG, "direcao = %d us", v);
    } else if (sscanf(l, "f %d", &v) == 1) {
        control_calib_servo(SERVO_FREIO, v);
        ESP_LOGI(TAG, "freio = %d us", v);
    } else if (sscanf(l, "r %d", &v) == 1) {
        control_calib_servo(SERVO_DRS, v);
        ESP_LOGI(TAG, "drs = %d us", v);
    } else if (sscanf(l, "ma %d", &v) == 1) {
        control_calib_motor(0, v);
        ESP_LOGI(TAG, "motor A a %d%% por 2 s", v);
    } else if (sscanf(l, "mb %d", &v) == 1) {
        control_calib_motor(1, v);
        ESP_LOGI(TAG, "motor B a %d%% por 2 s", v);
    } else {
        ESP_LOGW(TAG, "comando desconhecido: '%s' ('?' para ajuda)", l);
    }
}

static void console_task(void *arg)
{
    char linha[40];
    int n = 0;

    ajuda();
    while (true) {
        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(100)) <= 0) continue;

        if (c == '\r' || c == '\n') {
            if (n > 0) {
                linha[n] = '\0';
                uart_write_bytes(UART_NUM_0, "\r\n", 2);
                processar(linha);
                n = 0;
            }
        } else if (c == 8 || c == 127) {   // backspace
            if (n > 0) n--;
        } else if (n < (int)sizeof(linha) - 1) {
            linha[n++] = (char)c;
            uart_write_bytes(UART_NUM_0, (const char *)&c, 1);   // eco do que voce digita
        }
    }
}

void console_init(void)
{
    if (uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "nao consegui abrir a UART0: console desativado");
        return;
    }
    xTaskCreate(console_task, "console", 3072, NULL, 3, NULL);
}
