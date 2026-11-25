#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_log.h"

#include "act.h"
#include "config.h"
#include "util.h"

static const char *TAG = "act";

// Servos: timer 0, 50 Hz (periodo de 20 ms), 16 bits
#define SERVO_TIMER   LEDC_TIMER_0
#define SERVO_RES     LEDC_TIMER_16_BIT
#define SERVO_MAX     ((1 << 16) - 1)
#define SERVO_PER_US  20000

// Motores: timer 1, 20 kHz, 10 bits (0..1023)
#define MOTOR_TIMER   LEDC_TIMER_1
#define MOTOR_RES     LEDC_TIMER_10_BIT
#define MOTOR_MAX     ((1 << 10) - 1)

#define MODO LEDC_LOW_SPEED_MODE

static const ledc_channel_t CANAL_SERVO[3] = { LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2 };
static const int PINO_SERVO[3] = { PIN_SERVO_DIRECAO, PIN_SERVO_FREIO, PIN_SERVO_DRS };
#define CANAL_MOTOR_A LEDC_CHANNEL_3
#define CANAL_MOTOR_B LEDC_CHANNEL_4

static void configurar_canal(ledc_channel_t canal, int pino, ledc_timer_t timer)
{
    ledc_channel_config_t c = {
        .gpio_num   = pino,
        .speed_mode = MODO,
        .channel    = canal,
        .timer_sel  = timer,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&c));
}

static void escrever(ledc_channel_t canal, uint32_t duty)
{
    ledc_set_duty(MODO, canal, duty);
    ledc_update_duty(MODO, canal);
}

void act_init(void)
{
    // 1. Pinos de direcao dos motores e STBY: tudo em LOW antes de qualquer coisa
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_MOTOR_AIN1) | (1ULL << PIN_MOTOR_AIN2) |
                        (1ULL << PIN_MOTOR_BIN1) | (1ULL << PIN_MOTOR_BIN2) |
                        (1ULL << PIN_MOTOR_STBY),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_MOTOR_STBY, 0);
    gpio_set_level(PIN_MOTOR_AIN1, 0);
    gpio_set_level(PIN_MOTOR_AIN2, 0);
    gpio_set_level(PIN_MOTOR_BIN1, 0);
    gpio_set_level(PIN_MOTOR_BIN2, 0);

    // 2. Timers do PWM
    ledc_timer_config_t ts = {
        .speed_mode      = MODO,
        .duty_resolution = SERVO_RES,
        .timer_num       = SERVO_TIMER,
        .freq_hz         = 50,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ts));

    ledc_timer_config_t tm = {
        .speed_mode      = MODO,
        .duty_resolution = MOTOR_RES,
        .timer_num       = MOTOR_TIMER,
        .freq_hz         = MOTOR_PWM_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tm));

    // 3. Canais
    for (int i = 0; i < 3; i++) {
        configurar_canal(CANAL_SERVO[i], PINO_SERVO[i], SERVO_TIMER);
    }
    configurar_canal(CANAL_MOTOR_A, PIN_MOTOR_PWMA, MOTOR_TIMER);
    configurar_canal(CANAL_MOTOR_B, PIN_MOTOR_PWMB, MOTOR_TIMER);

    ESP_LOGI(TAG, "atuadores prontos (motores desligados)");
}

void act_servo_us(servo_t s, int us)
{
    if ((int)s < 0 || (int)s > 2) return;
    us = limitar(us, SERVO_US_MIN, SERVO_US_MAX);
    uint32_t duty = (uint32_t)us * SERVO_MAX / SERVO_PER_US;
    escrever(CANAL_SERVO[s], duty);
}

static void um_motor(int in1, int in2, ledc_channel_t canal, bool invertido, int pct)
{
    pct = limitar(pct, 0, 100);
    if (pct == 0) {
        gpio_set_level(in1, 0);   // roda livre
        gpio_set_level(in2, 0);
        escrever(canal, 0);
        return;
    }
    gpio_set_level(in1, invertido ? 0 : 1);
    gpio_set_level(in2, invertido ? 1 : 0);
    escrever(canal, (uint32_t)pct * MOTOR_MAX / 100);
}

void act_motores(bool habilita, int pct_a, int pct_b)
{
    if (!habilita) {
        pct_a = 0;
        pct_b = 0;
    }
    um_motor(PIN_MOTOR_AIN1, PIN_MOTOR_AIN2, CANAL_MOTOR_A, MOTOR_A_INVERTIDO, pct_a);
    um_motor(PIN_MOTOR_BIN1, PIN_MOTOR_BIN2, CANAL_MOTOR_B, MOTOR_B_INVERTIDO, pct_b);
    gpio_set_level(PIN_MOTOR_STBY, habilita ? 1 : 0);
}
