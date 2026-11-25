#pragma once
#include <stdint.h>

#define MAGIC_CMD 0xA5
#define MAGIC_TEL 0x5A

// Comando PC -> carro (6 bytes, mesma ordem do struct.pack "<BHBbB")
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint16_t seq;
    uint8_t  throttle;   // 0..100
    int8_t   steer;      // -100..100
    uint8_t  brake;      // 0..100
} cmd_pacote_t;

_Static_assert(sizeof(cmd_pacote_t) == 6, "cmd_pacote_t deve ter 6 bytes");

typedef enum {
    ESTADO_SEM_LINK  = 0,
    ESTADO_DESARMADO = 1,
    ESTADO_ARMADO    = 2,
    ESTADO_BAT_BAIXA = 3,
    ESTADO_CORTE     = 4,
} estado_t;

// Telemetria carro -> PC (10 bytes, mesma ordem do struct.pack "<BHBHBbBB")
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint16_t seq_eco;
    uint8_t  estado;
    uint16_t vbat_mv;
    uint8_t  throttle;
    int8_t   steer;
    uint8_t  brake;
    uint8_t  rx_por_s;
} tel_pacote_t;

_Static_assert(sizeof(tel_pacote_t) == 10, "tel_pacote_t deve ter 10 bytes");
