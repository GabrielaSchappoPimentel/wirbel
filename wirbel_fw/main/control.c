#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "act.h"
#include "battery.h"
#include "config.h"
#include "control.h"
#include "util.h"

static const char *TAG = "control";

static const char *NOMES[] = { "SEM_LINK", "DESARMADO", "ARMADO", "BAT_BAIXA", "CORTE" };

#define CAL_MOTOR_MS 2000

// Dados compartilhados entre tasks. So se mexe neles com o mutex pego.
typedef struct {
    // entrada da rede
    cmd_pacote_t cmd;
    bool         recebeu_algum;
    bool         tem_seq;
    uint16_t     ultimo_seq;
    uint32_t     ultimo_rx_ms;
    // entrada do console (calibracao)
    bool         calib;
    int          cal_us[3];
    int          cal_motor;      // 0 = A, 1 = B
    int          cal_motor_pct;
    uint32_t     cal_motor_ate;
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
    out->estado     = st.estado;
    out->throttle   = st.throttle;
    out->steer      = st.steer;
    out->brake      = st.brake;
    out->drs_pct    = st.drs_pct;
    out->vbat_mv    = st.vbat_mv;
    out->seq_eco    = st.ultimo_seq;
    out->calibrando = st.calib;
    xSemaphoreGive(mtx);
}

// ---------------- Calibracao ----------------
void control_calib(bool ativo)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    st.calib = ativo;
    st.cal_us[SERVO_DIRECAO] = DIR_CENTRO_US;
    st.cal_us[SERVO_FREIO]   = FREIO_SOLTO_US;
    st.cal_us[SERVO_DRS]     = DRS_FECHADO_US;
    st.cal_motor_pct = 0;
    xSemaphoreGive(mtx);
}

void control_calib_servo(servo_t s, int us)
{
    if ((int)s < 0 || (int)s > 2) return;
    xSemaphoreTake(mtx, portMAX_DELAY);
    st.cal_us[s] = limitar(us, SERVO_US_MIN, SERVO_US_MAX);
    xSemaphoreGive(mtx);
}

void control_calib_motor(int motor, int pct)
{
    xSemaphoreTake(mtx, portMAX_DELAY);
    st.cal_motor = motor ? 1 : 0;
    st.cal_motor_pct = limitar(pct, 0, 100);
    st.cal_motor_ate = agora_ms() + CAL_MOTOR_MS;
    xSemaphoreGive(mtx);
}

// ---------------- Conversoes para os servos ----------------
static int us_direcao(int steer)   // -100..100
{
    if (DIR_INVERTIDO) steer = -steer;
    return DIR_CENTRO_US + steer * DIR_AMPLITUDE_US / 100;
}

static int us_entre(int de_us, int ate_us, int pct)   // pct 0 -> de_us, 100 -> ate_us
{
    return de_us + (ate_us - de_us) * pct / 100;
}

// Abertura desejada da asa (0..100) conforme o quanto o carro esta virando
static int drs_alvo(int steer)
{
    int s = abs(steer);
    if (s <= DRS_ZONA_ABERTA)  return 100;
    if (s >= DRS_ZONA_FECHADA) return 0;
    return 100 * (DRS_ZONA_FECHADA - s) / (DRS_ZONA_FECHADA - DRS_ZONA_ABERTA);
}

// PWM maximo (%) para o N20 nao receber mais que MOTOR_V_MAX_MV
static int pct_maximo(int vbat_mv)
{
    if (vbat_mv <= MOTOR_V_MAX_MV) return 100;
    return MOTOR_V_MAX_MV * 100 / vbat_mv;
}

// ---------------- Task principal ----------------
static void control_task(void *arg)
{
    bool armado = false;
    bool corte = false;
    uint32_t abaixo_desde = 0;
    int vbat = bat_ler_mv();
    int drs = 0;
    int rampa = 0;
    int ciclo = 0;
    int estado_anterior = -1;
    bool calib_anterior = false;

    TickType_t ultimo = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&ultimo, pdMS_TO_TICKS(CONTROLE_PERIODO_MS));
        uint32_t agora = agora_ms();

        // 1. Copia as entradas (secao curta protegida)
        xSemaphoreTake(mtx, portMAX_DELAY);
        bool link_ok = st.recebeu_algum &&
                       (uint32_t)(agora - st.ultimo_rx_ms) < FAILSAFE_MS;
        cmd_pacote_t cmd = st.cmd;
        if (!link_ok) st.tem_seq = false;   // aceita seq reiniciado se o painel reabrir
        bool calib = st.calib;
        int cal_us[3] = { st.cal_us[0], st.cal_us[1], st.cal_us[2] };
        int cal_motor = st.cal_motor;
        int cal_pct = ((int32_t)(st.cal_motor_ate - agora) > 0) ? st.cal_motor_pct : 0;
        xSemaphoreGive(mtx);

        // 2. Bateria: le a cada 100 ms e filtra (media movel simples)
        if (++ciclo >= 10) {
            ciclo = 0;
            vbat = (3 * vbat + bat_ler_mv()) / 4;
        }
        // Abaixo de 1 V: nao ha bateria no divisor (ex.: so USB). Bloqueia sem travar.
        bool sem_bateria = vbat < 1000;
        if (!sem_bateria && vbat < VBAT_CORTE_MV) {
            if (abaixo_desde == 0) abaixo_desde = agora;
            if ((uint32_t)(agora - abaixo_desde) > VBAT_CORTE_ATRASO_MS && !corte) {
                corte = true;   // trava ate reiniciar
                ESP_LOGE(TAG, "BATERIA: corte por subtensao (%d mV). Recarregue.", vbat);
            }
        } else {
            abaixo_desde = 0;
        }

        if (calib != calib_anterior) {
            ESP_LOGW(TAG, "%s", calib ? "MODO CALIBRACAO: comandos do painel ignorados"
                                      : "calibracao encerrada");
            calib_anterior = calib;
            armado = false;
            rampa = 0;
        }

        // 3. Modo calibracao: servos na posicao pedida, motor so no teste de 2 s
        if (calib) {
            bool pode_motor = !sem_bateria && !corte && cal_pct > 0;
            int pct = cal_pct * pct_maximo(vbat) / 100;
            act_servo_us(SERVO_DIRECAO, cal_us[SERVO_DIRECAO]);
            act_servo_us(SERVO_FREIO,   cal_us[SERVO_FREIO]);
            act_servo_us(SERVO_DRS,     cal_us[SERVO_DRS]);
            act_motores(pode_motor, cal_motor == 0 ? pct : 0, cal_motor == 1 ? pct : 0);

            xSemaphoreTake(mtx, portMAX_DELAY);
            st.estado = ESTADO_DESARMADO;
            st.throttle = 0; st.steer = 0; st.brake = 0; st.drs_pct = 0;
            st.vbat_mv = (uint16_t)limitar(vbat, 0, 65535);
            xSemaphoreGive(mtx);
            continue;
        }

        // 4. Armar: so com o acelerador solto e o link bom
        if (!link_ok)               armado = false;
        else if (cmd.throttle == 0) armado = true;

        // 5. Estado e valores aplicados
        estado_t estado;
        int thr = cmd.throttle;
        int steer = cmd.steer;
        int brk = cmd.brake;
        if (brk > 0) thr = 0;   // freio tem prioridade sobre o acelerador

        if (!link_ok) {
            estado = ESTADO_SEM_LINK;  thr = 0; steer = 0; brk = 100;
        } else if (corte || sem_bateria) {
            estado = ESTADO_CORTE;     thr = 0; steer = 0; brk = 100;
        } else if (!armado) {
            estado = ESTADO_DESARMADO; thr = 0;
        } else if (vbat < VBAT_BAIXA_MV) {
            estado = ESTADO_BAT_BAIXA; thr /= 2;
        } else {
            estado = ESTADO_ARMADO;
        }

        // 6. Rampa do acelerador: sobe devagar, desce na hora
        if (thr > rampa) rampa = (thr - rampa > MOTOR_RAMPA_PASSO) ? rampa + MOTOR_RAMPA_PASSO : thr;
        else             rampa = thr;

        // 7. DRS: fecha na hora (sem link, corte, desarmado ou freando);
        //    senao segue a direcao, abrindo devagar e fechando mais rapido
        bool fecha_agora = (estado == ESTADO_SEM_LINK) || (estado == ESTADO_CORTE) ||
                           (estado == ESTADO_DESARMADO) || (brk > DRS_FREIO_LIMIAR);
        if (fecha_agora) {
            drs = 0;
        } else {
            int alvo = drs_alvo(steer);
            if (alvo > drs)      drs = (alvo - drs > DRS_PASSO_ABRE)  ? drs + DRS_PASSO_ABRE  : alvo;
            else if (alvo < drs) drs = (drs - alvo > DRS_PASSO_FECHA) ? drs - DRS_PASSO_FECHA : alvo;
        }

        // 8. Atuadores
        bool motores_ok = (estado == ESTADO_ARMADO || estado == ESTADO_BAT_BAIXA);
        int pct_motor = rampa * pct_maximo(vbat) / 100;
        act_motores(motores_ok, pct_motor, pct_motor);
        act_servo_us(SERVO_DIRECAO, us_direcao(steer));
        act_servo_us(SERVO_FREIO,   us_entre(FREIO_SOLTO_US, FREIO_APERTADO_US, brk));
        act_servo_us(SERVO_DRS,     us_entre(DRS_FECHADO_US, DRS_ABERTO_US, drs));

        // 9. Publica o resultado para a telemetria
        xSemaphoreTake(mtx, portMAX_DELAY);
        st.estado = estado;
        st.throttle = (uint8_t)rampa;
        st.steer = (int8_t)steer;
        st.brake = (uint8_t)brk;
        st.drs_pct = (uint8_t)drs;
        st.vbat_mv = (uint16_t)limitar(vbat, 0, 65535);
        xSemaphoreGive(mtx);

        if ((int)estado != estado_anterior) {
            ESP_LOGI(TAG, "estado: %s (vbat %d mV)", NOMES[estado], vbat);
            estado_anterior = (int)estado;
        }
    }
}

void control_init(void)
{
    mtx = xSemaphoreCreateMutex();
    configASSERT(mtx != NULL);

    act_init();
    bat_init();

    // Posicao segura antes de qualquer comando: freio apertado, asa fechada
    act_servo_us(SERVO_DIRECAO, DIR_CENTRO_US);
    act_servo_us(SERVO_FREIO,   FREIO_APERTADO_US);
    act_servo_us(SERVO_DRS,     DRS_FECHADO_US);

    st.estado = ESTADO_SEM_LINK;
    st.brake = 100;

    xTaskCreate(control_task, "controle", 4096, NULL, 6, NULL);
}
