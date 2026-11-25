#include <stdbool.h>

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_err.h"
#include "esp_log.h"

#include "battery.h"
#include "config.h"

static const char *TAG = "bat";

static adc_oneshot_unit_handle_t adc;
static adc_channel_t canal;
static adc_cali_handle_t cali;
static bool tem_cali = false;

void bat_init(void)
{
    adc_unit_t unidade;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(PIN_VBAT, &unidade, &canal));

    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = unidade };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&ucfg, &adc));

    // 12 dB: o pino le ate ~3,1 V. Com 8,4 V na bateria, o divisor entrega ~2,7 V.
    adc_oneshot_chan_cfg_t ccfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, canal, &ccfg));

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cc = {
        .unit_id = unidade,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    tem_cali = (adc_cali_create_scheme_line_fitting(&cc, &cali) == ESP_OK);
#endif
    if (!tem_cali) {
        ESP_LOGW(TAG, "sem calibracao de fabrica do ADC: leitura aproximada");
    }
}

int bat_ler_mv(void)
{
    int soma = 0;
    for (int i = 0; i < 8; i++) {
        int bruto = 0;
        if (adc_oneshot_read(adc, canal, &bruto) != ESP_OK) return 0;
        int mv;
        if (tem_cali) {
            adc_cali_raw_to_voltage(cali, bruto, &mv);
        } else {
            mv = bruto * 3100 / 4095;
        }
        soma += mv;
    }
    long pino_mv = soma / 8;
    long vbat = pino_mv * (VBAT_R_TOPO_OHM + VBAT_R_BASE_OHM) / VBAT_R_BASE_OHM;
    return (int)(vbat * VBAT_AJUSTE_PCT / 100);
}
