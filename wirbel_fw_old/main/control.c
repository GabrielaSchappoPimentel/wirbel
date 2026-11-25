#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "config.h"
#include "control.h"
#include "util.h"

static const char *TAG = "control";

static const char *NOMES[] = { "SEM_LINK", "DESARMADO", "ARMADO", "BAT_BAIXA", "CORTE" };

// Dados compartilhados entre a task de rede e a de controle.
// So se mexe neles com o mutex pego.
typedef struct {
    cmd_pacote_t cmd;           // ultimo comando aceito
    bool         recebeu_algum; // evita "link ok" falso logo apos o boot
    bool         tem_seq;
    uint16_t     ultimo_seq;
    uint32_t     ultimo_rx_ms;
    // saidas da task de controle (lidas pela telemetria)
    estado_t     estado;
    uint8_t      throttle;
    int8_t       steer;
    uint8_t      brake;
    uint8_t      drs_pct;
    uint16_t     vbat_mv;
} compartilhado_t;

static SemaphoreHandle_t mtx;
static compartilhado_t st;

const char *control_nome_estado(estado_t e)
{
    return (e <= ESTADO_CORTE) ? NOMES[e] : "?";
}

// true se seq e mais novo que o ultimo (trata a volta do contador em 65535)
static bool seq_novo(uint16_t seq, uint16_t ultimo, bool tem)
{
    if (!tem) return true;
    uint16_t delta = (uint16_t)(seq - ultimo);
    return delta > 0 && delta < 0x8000;
}

bool control_novo_comando(const cmd_pacote_t *c)
{
    cmd_pacote_t limpo = *c;
    if (limpo.throttle > 100) limpo.throttle = 100;
    if (limpo.brake > 100)    limpo.brake = 100;
    if (limpo.steer > 100)    limpo.steer = 100;
    if (limpo.steer < -100)   limpo.steer = -100;

    bool aceito = false;
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (seq_novo(limpo.seq, st.ultimo_seq, st.tem_seq)) {
        st.cmd = limpo;
        st.ultimo_seq = limpo.seq;
        st.tem_seq = true;
        st.ultimo_rx_ms = agora_ms();
        st.recebeu_algum = true;
        aceito = true;
    }
    xSemaphoreGive(mtx);
    return aceito;
}

void control_snapshot(control_snapshot_t *out)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    out->estado   = st.estado;
    out->throttle = st.throttle;
    out->steer    = st.steer;
    out->brake    = st.brake;
    out->drs_pct  = st.drs_pct;
    out->vbat_mv  = st.vbat_mv;
    out->seq_eco  = st.ultimo_seq;
    xSemaphoreGive(mtx);
}

// Provisorio: o ADC real entra no passo da bateria
static uint16_t ler_vbat_mv(void)
{
    return VBAT_PROVISORIA_MV;
}

// Abertura desejada da asa (0..100) conforme o quanto o carro esta virando
static uint8_t drs_alvo(int8_t steer)
{
    int s = abs(steer);
    if (s <= DRS_ZONA_ABERTA)  return 100;
    if (s >= DRS_ZONA_FECHADA) return 0;
    return (uint8_t)(100 * (DRS_ZONA_FECHADA - s) / (DRS_ZONA_FECHADA - DRS_ZONA_ABERTA));
}

static void control_task(void *arg)
{
    bool armado = false;
    bool corte = false;
    uint32_t abaixo_desde = 0;
    uint8_t drs_atual = 0;
    int estado_anterior = -1;

    TickType_t ultimo = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&ultimo, pdMS_TO_TICKS(CONTROLE_PERIODO_MS));
        uint32_t agora = agora_ms();

        // 1. Copia o comando e confere o link (secao curta protegida)
        xSemaphoreTake(mtx, portMAX_DELAY);
        bool link_ok = st.recebeu_algum &&
                       (uint32_t)(agora - st.ultimo_rx_ms) < FAILSAFE_MS;
        cmd_pacote_t cmd = st.cmd;
        if (!link_ok) st.tem_seq = false;   // aceita seq reiniciado se o painel reabrir
        xSemaphoreGive(mtx);

        // 2. Armar: so com o acelerador solto e o link bom
        if (!link_ok)               armado = false;
        else if (cmd.throttle == 0) armado = true;

        // 3. Bateria (corte so depois de 2 s abaixo do limite; trava ate reiniciar)
        uint16_t vbat = ler_vbat_mv();
        if (vbat < VBAT_CORTE_MV) {
            if (abaixo_desde == 0) abaixo_desde = agora;
            if ((uint32_t)(agora - abaixo_desde) > VBAT_CORTE_ATRASO_MS) corte = true;
        } else {
            abaixo_desde = 0;
        }

        // 4. Estado e valores aplicados
        estado_t estado;
        uint8_t thr = cmd.throttle;
        int8_t steer = cmd.steer;
        uint8_t brk = cmd.brake;
        if (brk > 0) thr = 0;   // freio tem prioridade sobre o acelerador

        if (!link_ok) {
            estado = ESTADO_SEM_LINK;  thr = 0; steer = 0; brk = 100;
        } else if (corte) {
            estado = ESTADO_CORTE;     thr = 0; steer = 0; brk = 100;
        } else if (!armado) {
            estado = ESTADO_DESARMADO; thr = 0;
        } else if (vbat < VBAT_BAIXA_MV) {
            estado = ESTADO_BAT_BAIXA; thr /= 2;
        } else {
            estado = ESTADO_ARMADO;
        }

        // 5. DRS: fecha na hora (sem link, corte, desarmado ou freando);
        //    senao segue a direcao, abrindo devagar e fechando mais rapido
        bool fecha_agora = (estado == ESTADO_SEM_LINK) || (estado == ESTADO_CORTE) ||
                           (estado == ESTADO_DESARMADO) || (brk > DRS_FREIO_LIMIAR);
        if (fecha_agora) {
            drs_atual = 0;
        } else {
            uint8_t alvo = drs_alvo(steer);
            if (alvo > drs_atual) {
                drs_atual = (alvo - drs_atual > DRS_PASSO_ABRE)
                                ? (uint8_t)(drs_atual + DRS_PASSO_ABRE) : alvo;
            } else if (alvo < drs_atual) {
                drs_atual = (drs_atual - alvo > DRS_PASSO_FECHA)
                                ? (uint8_t)(drs_atual - DRS_PASSO_FECHA) : alvo;
            }
        }

        // 6. Publica o resultado para a telemetria
        xSemaphoreTake(mtx, portMAX_DELAY);
        st.estado = estado;
        st.throttle = thr;
        st.steer = steer;
        st.brake = brk;
        st.drs_pct = drs_atual;
        st.vbat_mv = vbat;
        xSemaphoreGive(mtx);

        // (passo 5: aqui os valores thr, steer, brk e drs_atual vao para os servos e motores)

        if ((int)estado != estado_anterior) {
            ESP_LOGI(TAG, "estado: %s (vbat %u mV)", NOMES[estado], (unsigned)vbat);
            estado_anterior = (int)estado;
        }
    }
}

void control_init(void)
{
    mtx = xSemaphoreCreateMutex();
    configASSERT(mtx != NULL);

    st.estado = ESTADO_SEM_LINK;
    st.brake = 100;
    st.vbat_mv = VBAT_PROVISORIA_MV;

    ESP_LOGW(TAG, "bateria SIMULADA: %d mV (ate o passo da bateria)", VBAT_PROVISORIA_MV);
    xTaskCreate(control_task, "controle", 4096, NULL, 6, NULL);
}