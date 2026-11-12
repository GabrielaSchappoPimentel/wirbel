#pragma once
#include <stdint.h>

#define MAGIC_CMD 0xA5
#define MAGIC_TEL 0x5A

// Comando PC -> carro (6 bytes, little-endian, mesma ordem do struct.pack "<BHBbB")
typedef struct __attribute__((packed)) {
    uint8_t  magic;
    uint16_t seq;
    uint8_t  throttle;   // 0..100
    int8_t   steer;      // -100..100
    uint8_t  brake;      // 0..100
} cmd_pacote_t;

_Static_assert(sizeof(cmd_pacote_t) == 6, "cmd_pacote_t deve ter 6 bytes");