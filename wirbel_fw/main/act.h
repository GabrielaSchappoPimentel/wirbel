#pragma once
#include <stdbool.h>

// Atuadores: 3 servos e 2 motores. So a task de controle chama estas funcoes.

typedef enum {
    SERVO_DIRECAO = 0,
    SERVO_FREIO   = 1,
    SERVO_DRS     = 2,
} servo_t;

void act_init(void);

// Pulso do servo em microssegundos (limitado entre SERVO_US_MIN e SERVO_US_MAX)
void act_servo_us(servo_t s, int us);

// habilita = false desliga o driver (STBY em LOW).
// pct_a e pct_b: 0..100, sempre para frente (nao ha re). 0 = roda livre.
void act_motores(bool habilita, int pct_a, int pct_b);
