#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "protocol.h"

typedef struct {
    estado_t estado;
    uint8_t  throttle;   // o que o carro aplica (nao o que o PC pediu)
    int8_t   steer;
    uint8_t  brake;
    uint8_t  drs_pct;
    uint16_t vbat_mv;
    uint16_t seq_eco;
} control_snapshot_t;

void control_init(void);
bool control_novo_comando(const cmd_pacote_t *c);   // true se aceito
void control_snapshot(control_snapshot_t *out);
const char *control_nome_estado(estado_t e);